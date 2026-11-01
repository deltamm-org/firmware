// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/boot_record.h"

#include <cstdint>
#include <string_view>

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

/// The loader as the running image sees it.
struct LoaderStatus
{
    /// The image runs under DeltaMM's loader.
    bool present = false;
    /// On trial: it has not confirmed itself yet.
    bool trial = false;
    /// Only the PC confirms it.
    bool manual = false;
    std::uint8_t attempts = 0;
    std::uint8_t maxAttempts = 0;
    /// The previous image is kept and can be brought back.
    bool previousKept = false;
    /// Why the previous image came back last time.
    boot::Fallback fallback = boot::Fallback::None;
};

enum class LoaderRequest : std::uint8_t
{
    /// Wait in the loader for the PC.
    Stay,
    /// Bring the previous image back.
    Rollback,
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
    /// The board's own console commands, for experiments; false if
    /// @p command is none of them. debugHelp(): their summary, or nullptr.
    virtual bool debugCommand(std::string_view) { return false; }
    virtual const char *debugHelp() const { return nullptr; }
    /// Restarts into the loader with @p request. Returns false (and does
    /// not restart) when there is no loader.
    virtual bool enterLoader(LoaderRequest request)
    {
        static_cast<void>(request);
        return false;
    }
    /// Confirms the running image, so it stays after the next restart.
    /// True when it is confirmed (also when it already was).
    virtual bool confirmImage() { return false; }
    virtual LoaderStatus loaderStatus() { return {}; }

protected:
    ~SystemControl() = default;
};

} // namespace dmm::services
