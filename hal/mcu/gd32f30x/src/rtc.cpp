// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/hal/gd32f30x/rtc.h"

#include "dmm/hal/gd32f30x/registers.h"

// Set-up order from the GD32F30x User Manual, RTC and backup domain
// chapters: backup writes are enabled through the PMU, the clock source is
// chosen once per backup domain, and counter and prescaler writes happen in
// configuration mode after the previous write has finished.

namespace dmm::hal::gd32f30x {

namespace {

// Polls of a status bit before giving up; far longer than the RTC needs.
constexpr std::uint32_t PollLimit = 2'000'000;
// Polls for the crystal to settle, about 2 s at 120 MHz.
constexpr std::uint32_t CrystalPollLimit = 60'000'000;

bool waitFor(std::uintptr_t address, std::uint32_t bit, std::uint32_t limit = PollLimit)
{
    for (std::uint32_t i = 0; i < limit; ++i) {
        if ((reg(address) & bit) != 0) {
            return true;
        }
    }
    return false;
}

void enableBackupAccess()
{
    reg(rcu::Apb1En) |= rcu::BkpClock | rcu::PmuClock;
    [[maybe_unused]] const std::uint32_t enabled = reg(rcu::Apb1En);
    reg(pmu::Ctl) |= pmu::BackupWriteEnable;
}

void resynchronise()
{
    reg(rtc::Ctl) &= ~rtc::Synced;
    waitFor(rtc::Ctl, rtc::Synced);
    waitFor(rtc::Ctl, rtc::WriteFinished);
}

// Runs @p write with the counter and prescaler writable.
template <class F>
void configure(F write)
{
    waitFor(rtc::Ctl, rtc::WriteFinished);
    reg(rtc::Ctl) |= rtc::ConfigMode;
    write();
    reg(rtc::Ctl) &= ~rtc::ConfigMode;
    waitFor(rtc::Ctl, rtc::WriteFinished);
}

} // namespace

Rtc::Start Rtc::start()
{
    enableBackupAccess();
    const auto marker = static_cast<std::uint16_t>(reg(bkp::data(0)));
    if (marker == Marker || marker == OtherMarker) {
        resynchronise();
        return Start::Running;
    }
    reg(rcu::Bdctl) |= bdctl::BackupReset;
    reg(rcu::Bdctl) &= ~bdctl::BackupReset;
    reg(rcu::Bdctl) |= bdctl::LxtalOn;
    if (!waitFor(rcu::Bdctl, bdctl::LxtalStable, CrystalPollLimit)) {
        return Start::NoCrystal;
    }
    reg(rcu::Bdctl) = (reg(rcu::Bdctl) & ~bdctl::RtcSourceMask) | bdctl::RtcSourceLxtal | bdctl::RtcOn;
    resynchronise();
    // 32768 counts a second: the prescaler reloads at 32767.
    configure([] {
        reg(rtc::Psch) = 0;
        reg(rtc::Pscl) = 0x7FFF;
        reg(rtc::Cnth) = 0;
        reg(rtc::Cntl) = 0;
    });
    reg(bkp::data(0)) = Marker;
    return Start::Started;
}

std::uint32_t Rtc::seconds()
{
    // The two halves are read separately: read again if the high half
    // moved in between.
    for (;;) {
        const std::uint32_t high = reg(rtc::Cnth) & 0xFFFF;
        const std::uint32_t low = reg(rtc::Cntl) & 0xFFFF;
        if ((reg(rtc::Cnth) & 0xFFFF) == high) {
            return (high << 16) | low;
        }
    }
}

Rtc::Reading Rtc::read()
{
    // The divider counts down from 32767 to 0 within each second; read
    // again if the counter moved while the divider was read.
    for (;;) {
        const std::uint32_t before = seconds();
        const std::uint32_t divider = ((reg(rtc::Divh) & 0xF) << 16) | (reg(rtc::Divl) & 0xFFFF);
        if (seconds() == before) {
            return {before, static_cast<std::uint16_t>(0x7FFF - (divider & 0x7FFF))};
        }
    }
}

void Rtc::setCalibration(int steps)
{
    steps = steps > MaxCalibrationSteps ? MaxCalibrationSteps : steps < -MaxCalibrationSteps ? -MaxCalibrationSteps
                                                                                             : steps;
    std::uint32_t octl = reg(bkp::Octl) & ~(bkp::CalibrationMask | bkp::SpeedUp);
    octl |= static_cast<std::uint32_t>(steps < 0 ? -steps : steps);
    if (steps > 0) {
        octl |= bkp::SpeedUp;
    }
    reg(bkp::Octl) = octl;
}

int Rtc::calibration()
{
    const std::uint32_t octl = reg(bkp::Octl);
    const auto steps = static_cast<int>(octl & bkp::CalibrationMask);
    return (octl & bkp::SpeedUp) != 0 ? steps : -steps;
}

void Rtc::setSeconds(std::uint32_t seconds)
{
    configure([seconds] {
        reg(rtc::Cnth) = seconds >> 16;
        reg(rtc::Cntl) = seconds & 0xFFFF;
    });
}

} // namespace dmm::hal::gd32f30x
