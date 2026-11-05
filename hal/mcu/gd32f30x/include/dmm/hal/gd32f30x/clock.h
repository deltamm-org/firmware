// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::hal::gd32f30x {

inline constexpr std::uint32_t SystemClockHz = 120'000'000;

enum class ClockSource : std::uint8_t
{
    Crystal,
    Internal,
};

/// Runs the core and AHB at 120 MHz, APB2 at 120 MHz and APB1 at 60 MHz
/// through the PLL, with two flash wait states, and USB at 48 MHz. Uses the crystal of
/// @p crystalHz (a divisor of 120 MHz); if it does not start, uses the
/// internal 8 MHz oscillator instead, at the same frequencies but with RC
/// accuracy. Call once, before anything depends on a clock.
ClockSource startSystemClock(std::uint32_t crystalHz);

/// Where USB's 48 MHz comes from: the PLL divided by 2.5, or the internal
/// 48 MHz oscillator (IRC48M, untrimmed). Switches while USB runs, which
/// may upset the link. False if the oscillator did not start.
enum class UsbClockSource : std::uint8_t
{
    Pll,
    Internal48m,
};
bool selectUsbClock(UsbClockSource source);
UsbClockSource usbClock();

} // namespace dmm::hal::gd32f30x
