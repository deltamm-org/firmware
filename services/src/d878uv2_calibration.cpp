// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_calibration.h"

namespace dmm::services {

namespace {

// Byte 0x11: battery reading offset, 100 = none, in 10 mV steps.
constexpr std::uint32_t BatteryOffsetAt = 0x11;
constexpr std::uint8_t BatteryOffsetMax = 200;

constexpr std::uint32_t MHz = 1'000'000;

// Two 32-byte band blocks, UHF first. In each: power base per level at
// +2..+5, deviation at +6..+8, squelch base at +9, three little-endian
// front-end tune points at +10, +12 and +14. The UHF block's first word is
// the reference trim.
constexpr std::uint32_t UhfBlock = 0x000;
constexpr std::uint32_t VhfBlock = 0x020;
constexpr std::uint32_t PowerBaseAt = 2;
constexpr std::uint32_t DeviationAt = 6;
constexpr std::uint32_t SquelchBaseAt = 9;
constexpr std::uint32_t TunePointsAt = 10;
constexpr std::uint32_t BasebandGainsAt = 0x040;
// Two 39-byte baseband blocks, UHF then VHF. In each: converter offsets
// and gains at +0..+9, last-word bases per frequency range at +10, +12
// (and +14 on VHF); in the UHF block, codec trims at +0x12..+0x14 and the
// demodulator gain at +0x17.
constexpr std::uint32_t UhfBasebandBlock = 0x080;
constexpr std::uint32_t VhfBasebandBlock = 0x0C0;
constexpr std::uint32_t CodecTrimsAt = UhfBasebandBlock + 0x12;
constexpr std::uint32_t DemodGainAt = UhfBasebandBlock + 0x17;
// The digital voice gains, also in the UHF block, for both bands.
constexpr std::uint32_t SpeakerGainAt = UhfBasebandBlock + 0x19;
constexpr std::uint32_t MicGainAt = UhfBasebandBlock + 0x1B;
constexpr std::uint32_t ModGainAt = 0x10; // in each baseband block
// Slot timing: four big-endian words, then 'f' when written.
constexpr std::uint32_t SlotTimingAt = 0x100;
constexpr std::uint8_t SlotTimingValid = 'f';

// The calibration's slot for each power level, low to turbo.
unsigned powerSlot(PowerLevel level)
{
    switch (level) {
    case PowerLevel::Low:
        return 0;
    case PowerLevel::Mid:
        return 1;
    case PowerLevel::High:
        return 2;
    case PowerLevel::Turbo:
        return 3;
    }
    return 0;
}

struct Band
{
    std::uint32_t block;
    std::uint32_t lowHz;
    std::uint32_t highHz;
    // Power flatness: one byte per MHz from lowHz per level, tables
    // powerStride apart; the base value is calibrated at powerRef.
    std::uint32_t powerFlatness;
    std::uint32_t powerStride;
    unsigned powerRef;
    // Squelch flatness: one byte per squelchStepHz from lowHz.
    std::uint32_t squelchFlatness;
    std::uint32_t squelchStepHz;
    unsigned squelchEntries;
    unsigned squelchRef;
    // Front-end tune points at these frequencies; the last band edge is
    // pinned to fullTune when it is not a calibration point.
    std::uint32_t tuneHz[3];
    std::uint16_t tuneTop;
};

constexpr Band Vhf{VhfBlock, 136 * MHz, 174 * MHz, 0x400, 0x40, 19, 0x500, 2 * MHz, 20, 10,
    {136 * MHz, 155 * MHz, 174 * MHz}, 0};
constexpr Band Uhf{UhfBlock, 400 * MHz, 520 * MHz, 0x200, 0x80, 60, 0x520, 5 * MHz, 25, 12,
    {400 * MHz, 440 * MHz, 480 * MHz}, 0x1000};

const Band *bandFor(std::uint32_t hz)
{
    for (const Band *b : {&Vhf, &Uhf}) {
        if (hz >= b->lowHz && hz <= b->highHz) {
            return b;
        }
    }
    return nullptr;
}

std::uint16_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

constexpr int DacMax = 0xFFF;

bool copyValid(Storage &storage, std::uint32_t copy)
{
    constexpr std::uint32_t MarkerBytes = 8;
    std::uint8_t markers[MarkerBytes];
    if (!readable(storage.read(copy + D878Uv2Calibration::BlockBytes - MarkerBytes, markers))) {
        return false;
    }
    const bool valid = markers[4] == 0x55 && markers[5] == 0x55 && markers[6] == 0xAA && markers[7] == 0xAA;
    const bool superseded = markers[0] == 0xAA && markers[1] == 0xAA && markers[2] == 0x55 && markers[3] == 0x55;
    return valid && !superseded;
}

} // namespace

bool D878Uv2Calibration::load(Storage &storage)
{
    m_loaded.store(false, std::memory_order_relaxed);
    m_copy = copyValid(storage, CopyA) ? 0 : 1;
    const StorageResult r = storage.read(m_copy == 0 ? CopyA : CopyB, m_data);
    if (!readable(r)) {
        return false;
    }
    m_flagged = !usable(r);
    m_loaded.store(true, std::memory_order_release);
    return true;
}

const char *D878Uv2Calibration::source() const
{
    if (m_flagged) {
        return m_copy == 0 ? "copy A (flash check failed, as usual for this block)"
                           : "copy B (flash check failed, as usual for this block)";
    }
    return m_copy == 0 ? "copy A" : "copy B";
}

D878Uv2Calibration::D878Uv2Calibration(std::span<const std::uint8_t, Bytes> bytes)
{
    for (std::uint32_t i = 0; i < Bytes; ++i) {
        m_data[i] = bytes[i];
    }
    m_copy = 0;
    m_flagged = false;
    m_loaded.store(true, std::memory_order_release);
}

std::optional<int> D878Uv2Calibration::batteryOffsetMv() const
{
    if (!present() || m_data[BatteryOffsetAt] > BatteryOffsetMax) {
        return std::nullopt;
    }
    return (static_cast<int>(m_data[BatteryOffsetAt]) - 100) * 10;
}

std::optional<std::uint16_t> D878Uv2Calibration::referenceTrim() const
{
    if (!present()) {
        return std::nullopt;
    }
    const std::uint16_t v = le16(m_data + UhfBlock);
    if (v > DacMax) {
        return std::nullopt;
    }
    return v;
}

std::optional<std::uint16_t> D878Uv2Calibration::txPower(std::uint32_t hz, PowerLevel level) const
{
    const Band *b = bandFor(hz);
    if (!present() || b == nullptr) {
        return std::nullopt;
    }
    const unsigned l = powerSlot(level);
    const std::uint8_t *flatness = m_data + b->powerFlatness + l * b->powerStride;
    const unsigned seg = (hz - b->lowHz) / MHz;
    const int value = m_data[b->block + PowerBaseAt + l] + flatness[seg] - flatness[b->powerRef];
    constexpr int PowerMax = 0xFF;
    constexpr int PowerShift = 4;
    if (value < 0 || value > PowerMax) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value << PowerShift);
}

std::optional<std::uint16_t> D878Uv2Calibration::rxTune(std::uint32_t hz) const
{
    const Band *b = bandFor(hz);
    if (!present() || b == nullptr) {
        return std::nullopt;
    }
    // Points: the three calibrated ones, then the top band edge when it
    // has a fixed value of its own.
    std::uint32_t at[4];
    int value[4];
    unsigned n = 0;
    for (unsigned i = 0; i < 3; ++i) {
        at[n] = b->tuneHz[i];
        value[n++] = le16(m_data + b->block + TunePointsAt + 2 * i);
    }
    if (b->tuneTop != 0) {
        at[n] = b->highHz;
        value[n++] = b->tuneTop;
    }
    unsigned i = 0;
    while (i + 2 < n && hz > at[i + 1]) {
        ++i;
    }
    const auto span = static_cast<std::int64_t>(at[i + 1] - at[i]);
    const auto offset = static_cast<std::int64_t>(hz) - at[i];
    const std::int64_t v = value[i] + (value[i + 1] - value[i]) * offset / span;
    return static_cast<std::uint16_t>(v < 0 ? 0 : v > DacMax ? DacMax
                                                             : v);
}

std::optional<int> D878Uv2Calibration::squelchThreshold(std::uint32_t hz, int level) const
{
    const Band *b = bandFor(hz);
    if (!present() || b == nullptr) {
        return std::nullopt;
    }
    level = level < 1 ? 1 : level;
    unsigned seg = (hz - b->lowHz) / b->squelchStepHz;
    if (seg >= b->squelchEntries) {
        seg = b->squelchEntries - 1;
    }
    const std::uint8_t *flatness = m_data + b->squelchFlatness;
    return m_data[b->block + SquelchBaseAt] - 2 * (level - 1) + flatness[b->squelchRef] - flatness[seg];
}

std::optional<D878Uv2Calibration::Deviation> D878Uv2Calibration::deviation() const
{
    if (!present()) {
        return std::nullopt;
    }
    const std::uint8_t *d = m_data + UhfBlock + DeviationAt;
    return Deviation{d[0], d[1], d[2]};
}

std::optional<std::array<std::uint8_t, 12>> D878Uv2Calibration::basebandDcOffset(std::uint32_t hz) const
{
    if (!present() || bandFor(hz) == nullptr) {
        return std::nullopt;
    }
    // Last-word ranges: UHF below and from 460 MHz; VHF below 155 MHz,
    // to 200 MHz and from 200 MHz.
    const bool uhf = hz >= 300 * MHz;
    const std::uint32_t block = uhf ? UhfBasebandBlock : VhfBasebandBlock;
    const unsigned range = uhf ? (hz >= 460 * MHz ? 1 : 0) : (hz >= 200 * MHz ? 2 : hz >= 155 * MHz ? 1
                                                                                                    : 0);
    std::array<std::uint8_t, 12> d{};
    for (std::size_t i = 0; i < 10; ++i) {
        d[i] = m_data[block + i];
    }
    d[10] = m_data[block + 10 + 2 * range];
    d[11] = m_data[block + 11 + 2 * range];
    return d;
}

std::optional<std::array<std::uint8_t, 2>> D878Uv2Calibration::basebandDemodGain() const
{
    if (!present()) {
        return std::nullopt;
    }
    return std::array<std::uint8_t, 2>{m_data[DemodGainAt], m_data[DemodGainAt + 1]};
}

std::optional<std::array<std::uint8_t, 2>> D878Uv2Calibration::word(std::uint32_t at) const
{
    if (!present() || (m_data[at] == 0xFF && m_data[at + 1] == 0xFF)) {
        return std::nullopt;
    }
    return std::array<std::uint8_t, 2>{m_data[at], m_data[at + 1]};
}

std::optional<std::array<std::uint8_t, 2>> D878Uv2Calibration::basebandSpeakerGain() const
{
    return word(SpeakerGainAt);
}

std::optional<std::array<std::uint8_t, 2>> D878Uv2Calibration::basebandMicGain() const
{
    return word(MicGainAt);
}

std::optional<std::array<std::uint8_t, 2>> D878Uv2Calibration::basebandModGain(std::uint32_t hz) const
{
    if (!present() || bandFor(hz) == nullptr) {
        return std::nullopt;
    }
    const std::uint32_t at = (hz >= 300 * MHz ? UhfBasebandBlock : VhfBasebandBlock) + ModGainAt;
    return std::array<std::uint8_t, 2>{m_data[at], m_data[at + 1]};
}

std::optional<std::array<std::uint8_t, 8>> D878Uv2Calibration::basebandSlotTiming() const
{
    if (!present()) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 8> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        t[i] = m_data[SlotTimingAt + i];
    }
    // Not calibrated: a default offset, the rest as stored.
    if (m_data[SlotTimingAt + 8] != SlotTimingValid) {
        t[0] = 0x00;
        t[1] = 0x6E;
    }
    return t;
}

std::optional<std::array<std::uint8_t, 10>> D878Uv2Calibration::basebandAnalogGains() const
{
    if (!present()) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 10> g{};
    for (std::size_t i = 0; i < g.size(); ++i) {
        g[i] = m_data[BasebandGainsAt + i];
    }
    return g;
}

std::optional<D878Uv2Calibration::CodecTrims> D878Uv2Calibration::codecTrims() const
{
    if (!present()) {
        return std::nullopt;
    }
    const std::uint8_t *c = m_data + CodecTrimsAt;
    return CodecTrims{c[0], c[1], c[2]};
}

static_assert(Calibration<D878Uv2Calibration>);

} // namespace dmm::services
