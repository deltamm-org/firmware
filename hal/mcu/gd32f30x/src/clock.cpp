// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/clock.h"

#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

// Crystals usually start within a few milliseconds; this allows far more.
constexpr std::uint32_t CrystalStartPolls = 500'000;

bool waitFor(std::uintptr_t address, std::uint32_t mask, std::uint32_t value, std::uint32_t polls)
{
    while ((reg(address) & mask) != value) {
        if (polls-- == 0) {
            return false;
        }
    }
    return true;
}

} // namespace

ClockSource startSystemClock(std::uint32_t crystalHz)
{
    // Back to the internal oscillator with the PLL off, whatever a boot
    // loader left running: the PLL cannot be changed while it drives the core.
    reg(rcu::Ctl) |= rcu::Irc8mOn;
    waitFor(rcu::Ctl, rcu::Irc8mStable, rcu::Irc8mStable, 0xFFFFFFFFu);
    reg(rcu::Cfg0) &= ~rcu::SysClockMask;
    waitFor(rcu::Cfg0, rcu::SysClockStatusMask, rcu::SysClockIrc8m << 2, 0xFFFFFFFFu);
    reg(rcu::Ctl) &= ~(rcu::PllOn | rcu::ClockMonitorOn);
    reg(rcu::Apb1En) &= ~rcu::UsbdClock; // the USB divider is locked while USB runs
    reg(rcu::Cfg0) = 0;

    reg(rcu::Ctl) = (reg(rcu::Ctl) & ~rcu::HxtalBypass) | rcu::HxtalOn;
    const bool crystal = crystalHz != 0 && SystemClockHz % crystalHz == 0 &&
        waitFor(rcu::Ctl, rcu::HxtalStable, rcu::HxtalStable, CrystalStartPolls);
    if (!crystal) {
        reg(rcu::Ctl) &= ~rcu::HxtalOn;
    }

    reg(fmc::WaitState) = 2;
    const std::uint32_t pll = crystal ? rcu::PllFromHxtal | rcu::pllMultiplier(SystemClockHz / crystalHz)
                                      : rcu::pllMultiplier(SystemClockHz / 4'000'000);
    // USB takes the PLL divided by 2.5: 48 MHz.
    reg(rcu::Cfg0) = rcu::Apb1Div2 | rcu::UsbClockPllDiv2p5 | pll;
    reg(rcu::Ctl) |= rcu::PllOn;
    waitFor(rcu::Ctl, rcu::PllStable, rcu::PllStable, 0xFFFFFFFFu);
    reg(rcu::Cfg0) = (reg(rcu::Cfg0) & ~rcu::SysClockMask) | rcu::SysClockPll;
    waitFor(rcu::Cfg0, rcu::SysClockStatusMask, rcu::SysClockPll << 2, 0xFFFFFFFFu);
    return crystal ? ClockSource::Crystal : ClockSource::Internal;
}

bool selectUsbClock(UsbClockSource source)
{
    if (source == UsbClockSource::Pll) {
        reg(rcu::AddCtl) &= ~rcu::Usb48mFromIrc48m;
        reg(rcu::AddCtl) &= ~rcu::Irc48mOn;
        return true;
    }
    reg(rcu::AddCtl) |= rcu::Irc48mOn;
    if (!waitFor(rcu::AddCtl, rcu::Irc48mStable, rcu::Irc48mStable, CrystalStartPolls)) {
        reg(rcu::AddCtl) &= ~rcu::Irc48mOn;
        return false;
    }
    reg(rcu::AddCtl) |= rcu::Usb48mFromIrc48m;
    return true;
}

UsbClockSource usbClock()
{
    return (reg(rcu::AddCtl) & rcu::Usb48mFromIrc48m) != 0 ? UsbClockSource::Internal48m : UsbClockSource::Pll;
}

} // namespace dmm::hal::gd32f30x
