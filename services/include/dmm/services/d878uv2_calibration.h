// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/calibration.h"
#include "dmm/services/power_level.h"

#include <array>
#include <atomic>
#include <span>

namespace dmm::services {

/// The D878UVII's calibration: two copies of one 128 KB block, the first
/// 0x620 bytes used. A copy is valid when the last four bytes of its block
/// read 55 55 AA AA and the four before them do not read AA AA 55 55; when
/// the first copy is not valid, the second is used. Pages the flash flags
/// as uncorrectable are used as they are: these blocks are written in
/// pieces, which always fails the flash's own check.
///
/// Calibrated bands: 136-174 MHz and 400-520 MHz. The reference trim and
/// the power control are 12-bit DAC values, the front-end tuning a 12-bit
/// PWM value. Transmit power is the
/// band's base value (calibrated at 155 and 460 MHz) corrected by the
/// level's per-MHz flatness table, an 8-bit result that sets the DAC's top
/// eight bits; there is no value when the result leaves 0..255. Front-end
/// tuning is interpolated between the band's three calibration points.
/// The squelch threshold is the band's base corrected by its flatness
/// table. Deviation has one set for both bands.
class D878Uv2Calibration
{
public:
    static constexpr std::uint32_t CopyA = 0x02F60000;
    static constexpr std::uint32_t CopyB = 0x02F80000;
    static constexpr std::uint32_t BlockBytes = 0x20000;
    static constexpr std::uint32_t Bytes = 0x620;

    D878Uv2Calibration() = default;
    /// Uses @p bytes as the loaded copy, for tests.
    explicit D878Uv2Calibration(std::span<const std::uint8_t, Bytes> bytes);

    /// Call once, before any task reads values.
    bool load(Storage &storage);
    /// Read, and the factory's first-run marker is set. Once true, every
    /// value may be read from any task.
    bool present() const
    {
        return m_loaded.load(std::memory_order_acquire) && m_data[0x14] == 'w' && m_data[0x15] == 'w';
    }
    const char *source() const;

    bool loaded() const { return m_loaded.load(std::memory_order_acquire); }
    /// Which copy was read: 0 = first, 1 = second.
    int copy() const { return m_copy; }
    /// The flash flagged the copy as uncorrectable.
    bool flagged() const { return m_flagged; }

    std::optional<int> batteryOffsetMv() const;

    // RF values in the board's hardware units, for the board to hand to
    // the FM transceiver.

    /// FM transmit deviation settings: voice (with tones), CTCSS and DCS.
    struct Deviation
    {
        std::uint8_t voice;
        std::uint8_t ctcss;
        std::uint8_t dcs;
    };
    std::optional<std::uint16_t> referenceTrim() const;
    std::optional<std::uint16_t> txPower(std::uint32_t hz, PowerLevel level) const;
    std::optional<std::uint16_t> rxTune(std::uint32_t hz) const;
    std::optional<int> squelchThreshold(std::uint32_t hz, int level) const;
    std::optional<Deviation> deviation() const;

    /// Audio codec trims: left and right line-out gain, right input
    /// amplifier volume.
    struct CodecTrims
    {
        std::uint8_t leftOut;
        std::uint8_t rightOut;
        std::uint8_t rightInput;
    };
    std::optional<CodecTrims> codecTrims() const;
    /// The baseband chip's analog-mode gains, big-endian words: transmit
    /// before and after the limiter, CTCSS, DCS and receive volume.
    std::optional<std::array<std::uint8_t, 10>> basebandAnalogGains() const;
    /// The baseband chip's converter offsets and output gains for @p hz, as
    /// stored (big-endian words): the band block's ten bytes, then the
    /// frequency range's own last word. The per-MHz correction of that last
    /// word is not applied.
    std::optional<std::array<std::uint8_t, 12>> basebandDcOffset(std::uint32_t hz) const;
    /// The baseband chip's demodulator gain, as stored (big-endian).
    std::optional<std::array<std::uint8_t, 2>> basebandDemodGain() const;
    /// The baseband chip's modulator gain for @p hz's band, as stored
    /// (big-endian).
    std::optional<std::array<std::uint8_t, 2>> basebandModGain(std::uint32_t hz) const;
    /// The baseband chip's transmit slot timing offset and length, as
    /// stored (big-endian words, 41.7 us units, then two more words).
    std::optional<std::array<std::uint8_t, 8>> basebandSlotTiming() const;

    const std::uint8_t *data() const { return m_data; }

private:
    std::atomic<bool> m_loaded{false};
    int m_copy = 0;
    bool m_flagged = false;
    std::uint8_t m_data[Bytes]{};
};

} // namespace dmm::services
