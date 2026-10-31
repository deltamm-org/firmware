// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/tone_synth.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// The bits of one HDLC frame as they go on air: opening flags, the frame's
/// bytes least significant bit first with a 0 stuffed after five 1s in a
/// row, then closing flags. NRZI is the modem's part.
class HdlcBits
{
public:
    static constexpr std::uint8_t Flag = 0x7E;

    /// Starts on @p frame, which must stay as it is until the last bit,
    /// with @p leadFlags and @p tailFlags flags around it.
    void start(std::span<const std::uint8_t> frame, std::uint16_t leadFlags, std::uint8_t tailFlags);

    /// The next bit, 0 or 1; -1 once the last flag has been sent.
    int next();

private:
    enum class Part : std::uint8_t
    {
        Lead,
        Frame,
        Tail,
        Done,
    };

    std::span<const std::uint8_t> m_frame;
    std::size_t m_index = 0;
    std::uint16_t m_flagsLeft = 0;
    std::uint8_t m_tailFlags = 0;
    std::uint8_t m_byte = 0;
    std::uint8_t m_bit = 8;
    std::uint8_t m_ones = 0;
    bool m_stuff = false;
    Part m_part = Part::Done;
};

/// Bell 202 AFSK for 1200 baud packet: a 1200 Hz (mark) or 2200 Hz (space)
/// tone, changing at each 0 bit (NRZI) without a phase jump, as unsigned
/// output levels (for example PWM duty) at a fixed sample rate.
///
/// start() runs in a task while the modulator is idle; next() and
/// running() in the sample interrupt.
class AfskModulator
{
public:
    static constexpr std::uint32_t BaudRate = 1200;
    static constexpr std::uint32_t MarkHz = 1200;
    static constexpr std::uint32_t SpaceHz = 2200;

    /// Starts sending @p frame (from the first address to the frame check
    /// sequence, unchanged until it is sent) with the given flags around
    /// it, at @p peak output level.
    void start(std::span<const std::uint8_t> frame, std::uint16_t leadFlags, std::uint8_t tailFlags, std::uint32_t sampleRateHz,
        std::uint8_t peak);

    /// True until the last flag's last sample.
    bool running() const { return m_running; }

    /// The next output level; 0 when not running.
    std::uint8_t next();

private:
    void nextBit();

    HdlcBits m_bits;
    std::uint32_t m_phase = 0;
    std::uint32_t m_markStep = 0;
    std::uint32_t m_spaceStep = 0;
    std::uint32_t m_bitPhase = 0;
    std::uint32_t m_bitStep = 0;
    std::uint8_t m_peak = 0;
    bool m_space = false;
    volatile bool m_running = false;
};

} // namespace dmm::services
