// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// What the previous run left behind when it crashed.
///
/// The fields after lr follow the one CPU family supported today. A board
/// on another CPU would need a generic list of named values here.
struct CrashReport
{
    /// "hard fault", "stack overflow", ...
    const char *kind = nullptr;
    std::uint32_t detail = 0;
    std::uint32_t pc = 0;
    std::uint32_t lr = 0;
    std::uint32_t psr = 0;
    std::uint32_t faultStatus = 0;
    std::uint32_t hardFaultStatus = 0;
    std::uint32_t faultAddress = 0;
    char task[12] = {};
};

struct ResetReport
{
    /// "power-on", "watchdog", ...
    const char *cause = "unknown";
    /// The board's raw reset flags.
    std::uint32_t flags = 0;
    bool crashed = false;
    CrashReport crash;
};

/// Board-level power and supervision: the power switch, the power hold
/// line, the hardware watchdog and restart.
class SystemControl
{
public:
    /// True while the power switch is in its off position.
    virtual bool powerSwitchOff() = 0;
    /// Keeps the radio powered after the switch is turned off.
    virtual void holdPower() = 0;
    /// Lets the radio power down once the switch is off.
    virtual void releasePower() = 0;
    /// Restarts the hardware watchdog's countdown.
    virtual void feedWatchdog() = 0;
    /// Restarts the MCU. Does not return on a radio.
    virtual void restart() = 0;
    /// Why the previous run ended.
    virtual ResetReport lastReset() = 0;
    /// Crashes on purpose, to test crash capture. Does not return on a radio.
    virtual void testFault() = 0;

protected:
    ~SystemControl() = default;
};

} // namespace dmm::services
