// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::hal::gd32f30x {

enum class ResetCause : std::uint8_t
{
    PowerOn,
    ResetPin,
    Software,
    Watchdog,
    WindowWatchdog,
    LowPower,
    Unknown,
};

struct ResetFlags
{
    ResetCause cause;
    /// The raw flag bits (RCU_RSTSCK bits 31:26).
    std::uint32_t raw;
};

/// Reads why the MCU last reset and clears the flags. Call once at start.
ResetFlags takeResetFlags();

/// Reads the reset flags and leaves them set. They gather over resets
/// until cleared.
ResetFlags readResetFlags();

} // namespace dmm::hal::gd32f30x
