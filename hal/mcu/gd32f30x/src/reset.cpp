// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/reset.h"

#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

ResetFlags takeResetFlags()
{
    const ResetFlags flags = readResetFlags();
    reg(rcu::ResetSource) |= rcu::ClearResetFlags;
    return flags;
}

ResetFlags readResetFlags()
{
    const std::uint32_t raw = reg(rcu::ResetSource) & rcu::ResetFlagsMask;

    // Several flags can be set at once (a power-on also sets the pin flag);
    // the most specific one wins.
    ResetCause cause = ResetCause::Unknown;
    if ((raw & rcu::ResetByFreeWatchdog) != 0) {
        cause = ResetCause::Watchdog;
    } else if ((raw & rcu::ResetByWindowWatchdog) != 0) {
        cause = ResetCause::WindowWatchdog;
    } else if ((raw & rcu::ResetBySoftware) != 0) {
        cause = ResetCause::Software;
    } else if ((raw & rcu::ResetByLowPower) != 0) {
        cause = ResetCause::LowPower;
    } else if ((raw & rcu::ResetByPower) != 0) {
        cause = ResetCause::PowerOn;
    } else if ((raw & rcu::ResetByPin) != 0) {
        cause = ResetCause::ResetPin;
    }
    return {cause, raw};
}

} // namespace dmm::hal::gd32f30x
