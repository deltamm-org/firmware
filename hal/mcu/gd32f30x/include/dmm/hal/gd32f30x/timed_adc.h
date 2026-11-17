// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::hal::gd32f30x {

/// ADC2 converting one input at a fixed rate: TIMER0 channel 2 starts each
/// conversion, and DMA1 channel 4 copies the 12-bit results (0..4095) into
/// a ring, round and round, with no interrupts. The ADC clock is APB2 / 8
/// (set by Adc0::start, which must run first). The board sets the input
/// pin to analog. TIMER0's channel 2 output stays off its pin as long as
/// that pin is not in an alternate-function mode.
class TimedAdc
{
public:
    /// Powers ADC2 up, calibrates it and starts converting @p channel
    /// (0..17) into @p ring at SystemClockHz / round(SystemClockHz /
    /// @p rateHz). False if the rate is out of range (the conversion must
    /// fit in one period) or the ring is empty or longer than 65535.
    bool start(unsigned channel, std::uint32_t rateHz, std::span<std::uint16_t> ring);

    /// Stops converting and powers ADC2 down.
    void stop();

    /// The ring index the next result goes to.
    std::size_t position() const;

private:
    std::size_t m_size = 0;
};

} // namespace dmm::hal::gd32f30x
