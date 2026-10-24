// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_codeplug.h"

#include <algorithm>
#include <iterator>

namespace dmm::services {

namespace {

// Codeplug addresses.
constexpr std::uint32_t ChannelBanks = 0x00800000;
constexpr std::uint32_t ChannelBankStride = 0x00040000;
constexpr unsigned ChannelsPerBank = 128;
constexpr std::uint32_t ChannelBytes = 0x40;
constexpr std::uint32_t ZoneChannels = 0x01000000;
constexpr std::uint32_t ZoneChannelStride = 0x200;
constexpr std::uint32_t Settings = 0x02500000;
constexpr std::uint32_t SettingsBytes = 0x100;
constexpr std::uint32_t ZoneSelections = 0x02500100; // u16 list position per zone, side A then B
constexpr std::uint32_t ZoneSelectionSideB = 0x200;
constexpr std::uint32_t ZoneNames = 0x02540000;
constexpr std::uint32_t ZoneNameStride = 0x20;
constexpr std::uint32_t ZoneBitmap = 0x024C1300;
constexpr std::uint32_t ChannelBitmap = 0x024C1500;
constexpr std::uint32_t VfoA = 0x00FC0800;
constexpr std::uint32_t VfoB = 0x00FC0840;

constexpr std::uint32_t BlockBytes = 0x20000;
constexpr std::uint32_t MarkerOffset = BlockBytes - 8;

// CTCSS tones by their codeplug index, in 0.1 Hz; index 51 is the
// channel's own tone.
constexpr std::uint16_t CtcssTable[] = {625, 670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000,
    1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713,
    1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503,
    2541};
constexpr std::uint8_t CustomCtcss = 51;

std::uint16_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

// Big-endian BCD, eight digits.
std::optional<std::uint32_t> bcd8(const std::uint8_t *p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        for (const unsigned digit : {static_cast<unsigned>(p[i] >> 4), static_cast<unsigned>(p[i] & 0xF)}) {
            if (digit > 9) {
                return std::nullopt;
            }
            v = v * 10 + digit;
        }
    }
    return v;
}

Tone decodeTone(unsigned kind, std::uint8_t ctcssIndex, std::uint16_t dcs, std::uint16_t customCtcss)
{
    Tone t;
    if (kind == 1) {
        if (ctcssIndex < CustomCtcss) {
            t.kind = Tone::Kind::Ctcss;
            t.value = CtcssTable[ctcssIndex];
        } else if (ctcssIndex == CustomCtcss && customCtcss != 0) {
            t.kind = Tone::Kind::Ctcss;
            t.value = customCtcss;
        }
    } else if (kind == 2) {
        t.kind = Tone::Kind::Dcs;
        t.inverted = dcs >= 512;
        t.value = static_cast<std::uint16_t>(dcs % 512);
    }
    return t;
}

} // namespace

std::optional<Channel> decodeChannel(std::span<const std::uint8_t, 64> r)
{
    const auto rx = bcd8(&r[0]);
    const auto offset = bcd8(&r[4]);
    if (!rx || !offset || *rx == 0) {
        return std::nullopt;
    }
    Channel c;
    c.receiveHz = *rx * 10;
    const std::uint32_t offsetHz = *offset * 10;
    switch ((r[8] >> 6) & 3) {
    case 1:
        c.transmitHz = c.receiveHz + offsetHz;
        break;
    case 2:
        c.transmitHz = c.receiveHz - offsetHz;
        break;
    default:
        c.transmitHz = c.receiveHz;
        break;
    }
    c.mode = static_cast<ChannelMode>(r[8] & 3);
    c.power = static_cast<PowerLevel>((r[8] >> 2) & 3);
    c.wide = (r[8] & 0x10) != 0;
    c.receiveOnly = (r[9] & 0x20) != 0;
    const std::uint16_t custom = le16(&r[0x10]);
    c.receiveTone = decodeTone(r[9] & 3, r[0x0B], le16(&r[0x0E]), custom);
    c.transmitTone = decodeTone((r[9] >> 2) & 3, r[0x0A], le16(&r[0x0C]), custom);
    std::size_t n = 0;
    for (; n < Channel::NameBytes && r[0x23 + n] != 0; ++n) {
        c.name[n] = static_cast<char>(r[0x23 + n]);
    }
    c.name[n] = '\0';
    return c;
}

CodeplugSettings decodeSettings(std::span<const std::uint8_t> b)
{
    CodeplugSettings s;
    if (b.size() < 0xC0) {
        return s;
    }
    auto squelch = [](std::uint8_t v) { return static_cast<std::uint8_t>(v > 5 ? 3 : v); };
    s.squelchA = squelch(b[0x09]);
    s.squelchB = squelch(b[0x0A]);
    s.transmitTimeoutS = static_cast<std::uint16_t>(b[0x04] * 30);
    s.vfoA = b[0x15] != 0;
    s.vfoB = b[0x16] != 0;
    s.zoneA = b[0x1F];
    s.zoneB = b[0x20];
    s.sideBMain = b[0x2C] != 0;
    const std::uint8_t lock = b[0xBE];
    s.keyLock.knob = (lock & 0x01) != 0;
    s.keyLock.keypad = (lock & 0x02) != 0;
    s.keyLock.side = (lock & 0x08) != 0;
    s.keyLock.forced = (lock & 0x10) != 0;
    // Auto key lock follows the menu exit time: 5 s steps from 5 s.
    const unsigned exitSteps = b[0x37] > 11 ? 11 : b[0x37];
    s.keyLock.autoLockMs = b[0x02] != 0 ? (exitSteps + 1) * 5000 : 0;
    s.batterySave = b[0x0B] <= 2 ? b[0x0B] : 1;
    constexpr std::uint16_t BacklightTimes[] = {0, 5, 10, 15, 20, 25, 30, 60, 120, 180, 240, 300, 900, 1800, 2700, 3600};
    s.backlightS = b[0x27] < std::size(BacklightTimes) ? BacklightTimes[b[0x27]] : 0;
    return s;
}

bool D878Uv2Codeplug::copyValid(std::uint32_t block, bool &valid)
{
    for (std::size_t i = 0; i < m_cachedCount; ++i) {
        if (m_cachedBlock[i] == block) {
            valid = m_cachedValid[i];
            return true;
        }
    }
    std::uint8_t m[8];
    if (!readable(m_storage.read(block + MarkerOffset, m))) {
        return false;
    }
    const bool marked = m[4] == 0x55 && m[5] == 0x55 && m[6] == 0xAA && m[7] == 0xAA;
    const bool superseded = m[0] == 0xAA && m[1] == 0xAA && m[2] == 0x55 && m[3] == 0x55;
    valid = marked && !superseded;
    const std::size_t slot = m_cachedCount < Cached ? m_cachedCount++ : block / BlockBytes % Cached;
    m_cachedBlock[slot] = block;
    m_cachedValid[slot] = valid;
    return true;
}

StorageResult D878Uv2Codeplug::read(std::uint32_t address, std::span<std::uint8_t> data)
{
    StorageResult worst = StorageResult::Ok;
    while (!data.empty()) {
        const std::uint32_t block = address - address % BlockBytes;
        const std::size_t n = std::min<std::size_t>(data.size(), block + BlockBytes - address);
        bool firstValid = false;
        if (!copyValid(block, firstValid)) {
            return StorageResult::Timeout;
        }
        const StorageResult r = m_storage.read(firstValid ? address : address + BlockBytes, data.first(n));
        if (!readable(r)) {
            return r;
        }
        if (r != StorageResult::Ok) {
            worst = r;
        }
        address += static_cast<std::uint32_t>(n);
        data = data.subspan(n);
    }
    return worst;
}

std::optional<CodeplugSettings> D878Uv2Codeplug::settings()
{
    std::uint8_t b[SettingsBytes];
    if (!readable(read(Settings, b))) {
        return std::nullopt;
    }
    return decodeSettings(b);
}

bool D878Uv2Codeplug::bitSet(std::uint32_t bitmap, unsigned index)
{
    std::uint8_t byte = 0;
    return readable(read(bitmap + index / 8, std::span(&byte, 1))) && (byte >> (index % 8) & 1) != 0;
}

std::optional<Channel> D878Uv2Codeplug::channel(std::uint16_t index)
{
    if (index >= MaxChannels || !bitSet(ChannelBitmap, index)) {
        return std::nullopt;
    }
    const std::uint32_t address =
        ChannelBanks + (index / ChannelsPerBank) * ChannelBankStride + (index % ChannelsPerBank) * ChannelBytes;
    std::uint8_t r[ChannelBytes];
    if (!readable(read(address, r))) {
        return std::nullopt;
    }
    return decodeChannel(std::span<const std::uint8_t, 64>(r));
}

std::optional<Channel> D878Uv2Codeplug::vfo(bool sideB)
{
    std::uint8_t r[ChannelBytes];
    if (!readable(read(sideB ? VfoB : VfoA, r))) {
        return std::nullopt;
    }
    return decodeChannel(std::span<const std::uint8_t, 64>(r));
}

std::size_t D878Uv2Codeplug::zonesInUse(std::span<std::uint8_t> out)
{
    std::uint8_t bitmap[(MaxZones + 7) / 8];
    if (!readable(read(ZoneBitmap, bitmap))) {
        return 0;
    }
    std::size_t n = 0;
    for (unsigned z = 0; z < MaxZones && n < out.size(); ++z) {
        if ((bitmap[z / 8] >> (z % 8) & 1) != 0) {
            out[n++] = static_cast<std::uint8_t>(z);
        }
    }
    return n;
}

bool D878Uv2Codeplug::zoneInUse(std::uint8_t zone)
{
    return zone < MaxZones && bitSet(ZoneBitmap, zone);
}

bool D878Uv2Codeplug::zoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name)
{
    std::uint8_t raw[ZoneNameBytes];
    name[0] = '\0';
    if (zone >= MaxZones || !readable(read(ZoneNames + zone * ZoneNameStride, raw))) {
        return false;
    }
    std::size_t n = 0;
    for (; n < ZoneNameBytes && raw[n] != 0 && raw[n] != 0xFF; ++n) {
        name[n] = static_cast<char>(raw[n]);
    }
    name[n] = '\0';
    return true;
}

std::size_t D878Uv2Codeplug::zoneChannels(std::uint8_t zone, std::span<std::uint16_t> out)
{
    std::uint8_t raw[MaxZoneChannels * 2];
    if (zone >= MaxZones || !readable(read(ZoneChannels + zone * ZoneChannelStride, raw))) {
        return 0;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < MaxZoneChannels && n < out.size(); ++i) {
        const std::uint16_t c = le16(&raw[2 * i]);
        if (c != NoChannel && c < MaxChannels) {
            out[n++] = c;
        }
    }
    return n;
}

std::uint16_t D878Uv2Codeplug::zoneSelection(std::uint8_t zone, bool sideB)
{
    std::uint8_t raw[2];
    if (zone >= MaxZones || !readable(read(ZoneSelections + (sideB ? ZoneSelectionSideB : 0) + zone * 2u, raw))) {
        return NoChannel;
    }
    return le16(raw);
}

} // namespace dmm::services
