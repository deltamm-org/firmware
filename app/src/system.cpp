// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "system.h"

#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/os/os.h"
#include "dmm/services/battery.h"
#include "dmm/services/calibration.h"
#include "dmm/services/log.h"
#include "dmm/services/power.h"
#include "dmm/version.h"

#include <atomic>

namespace dmm::app::system {

namespace {

using services::Leds;
namespace log = services::log;

Platform *platform = nullptr;
os::Task<384> systemTask;

constexpr os::Ticks StepMs = 10;
constexpr os::Ticks StartFlashMs = 300;
constexpr os::Ticks HeartbeatPeriodMs = 1000;
constexpr os::Ticks HeartbeatOnMs = 100;
constexpr os::Ticks UptimeReportMs = 10000;
constexpr os::Ticks BatteryEveryMs = 250;
constexpr os::Ticks LowBatteryBlinkMs = 800;
// Time for the link task to send the last log lines before power goes.
constexpr os::Ticks LogFlushMs = 200;

std::atomic<bool> feedingStopped{false};
std::atomic<os::Ticks> lastActivity{0};
std::atomic<int> autoOffMinutes{0};
std::atomic<bool> batteryReportWanted{false};

services::BatteryMonitor battery(services::BatteryMonitor::TwoCellLiIon);
services::StockCalibration stockCalibration;
std::atomic<bool> calibrationReady{false};
bool batteryCalibrated = false;

void feedWatchdog()
{
    if (!feedingStopped.load(std::memory_order_relaxed)) {
        platform->system.feedWatchdog();
    }
}

void logResetReport()
{
    const services::ResetReport r = platform->system.lastReset();
    log::info("system") << "last reset: " << r.cause << " (flags " << lib::hex(r.flags) << ")";
    if (r.crashed) {
        const services::CrashReport &c = r.crash;
        log::error("system") << "crash: " << c.kind << " in task " << (c.task[0] != '\0' ? c.task : "-") << ", detail "
                             << c.detail << ", pc " << lib::hex(c.pc) << ", lr " << lib::hex(c.lr);
        log::error("system") << "crash: psr " << lib::hex(c.psr) << ", fault status " << lib::hex(c.faultStatus)
                             << ", hard fault status " << lib::hex(c.hardFaultStatus) << ", address "
                             << lib::hex(c.faultAddress);
    }
}

// Millivolts as volts with two decimals, such as "7.42 V".
lib::StaticString<12> volts(int millivolts)
{
    const int cv = (millivolts + 5) / 10;
    lib::StaticString<12> text;
    text << cv / 100 << '.' << static_cast<char>('0' + cv / 10 % 10) << static_cast<char>('0' + cv % 10) << " V";
    return text;
}

void logBattery(const char *what)
{
    log::info("battery") << what << volts(battery.millivolts()).view() << ", level " << battery.level()
                         << (battery.low() ? ", low" : "") << (batteryCalibrated ? "" : " (uncalibrated)");
}

// Waits until the power switch is turned off, then lets go of the power
// hold: the radio switches off. If power stays (USB), turning the switch
// back on restarts the radio.
void waitForSwitchOffThenPowerDown()
{
    bool switchWasOff = false;
    for (;;) {
        feedWatchdog();
        os::sleepMs(StepMs);
        if (os::stopRequested()) {
            return;
        }
        const bool off = platform->system.powerSwitchOff();
        if (off && switchWasOff) {
            break;
        }
        switchWasOff = off;
    }
    log::info("system") << "power switch off";
    platform->leds.set(Leds::Led::Green, false);
    platform->leds.set(Leds::Led::Red, false);
    feedWatchdog();
    os::sleepMs(LogFlushMs);
    platform->system.releasePower();
    bool switchWasOn = false;
    while (!os::stopRequested()) {
        feedWatchdog();
        os::sleepMs(StepMs);
        const bool on = !platform->system.powerSwitchOff();
        if (on && switchWasOn) {
            log::info("system") << "power switch on, restarting";
            platform->system.restart();
            return;
        }
        switchWasOn = on;
    }
}

// Auto power-off and an empty battery: with the switch still on the power
// cannot be cut, so the radio goes dark and waits for the switch.
void softOff(const char *reason)
{
    log::info("system") << "switching off: " << reason << "; turn the power knob off and on to restart";
    ui::post(ui::SwitchingOff{});
    os::sleepMs(2000);
    ui::post(ui::Backlight{0});
    ui::post(ui::PanelPower{false});
    platform->leds.set(Leds::Led::Green, false);
    platform->leds.set(Leds::Led::Red, false);
    waitForSwitchOffThenPowerDown();
}

// Reports the data flash and takes the battery offset from the radio's
// calibration.
void loadCalibration()
{
    if (platform->storage == nullptr) {
        return;
    }
    const services::StorageInfo info = platform->storage->info();
    if (!info.present) {
        log::error("storage") << "no data flash found (id " << lib::hex(info.maker, 2) << " " << lib::hex(info.device, 2)
                              << "); codeplug and calibration unavailable";
        return;
    }
    log::info("storage") << "flash " << lib::hex(info.maker, 2) << " " << lib::hex(info.device, 2) << ", "
                         << info.sizeBytes / (1024 * 1024) << " MB, "
                         << (info.remappedBlocks < 0 ? "no remap table" : "remapped blocks: ")
                         << (info.remappedBlocks < 0 ? 0 : info.remappedBlocks);
    if (!stockCalibration.load(*platform->storage)) {
        log::error("calibration") << "could not be read";
        return;
    }
    calibrationReady.store(stockCalibration.present(), std::memory_order_release);
    const auto offset = stockCalibration.batteryOffsetMv();
    if (!stockCalibration.present() || !offset) {
        log::warning("calibration") << "copy " << (stockCalibration.copy() == 0 ? "A" : "B")
                                    << " read, but the radio does not look calibrated";
        return;
    }
    battery.setOffset(*offset);
    batteryCalibrated = true;
    log::info("calibration") << "copy " << (stockCalibration.copy() == 0 ? "A" : "B") << ", battery offset "
                             << (*offset < 0 ? "-" : "+") << (*offset < 0 ? -*offset : *offset) << " mV"
                             << (stockCalibration.flagged() ? " (flash check failed, as usual for this block)" : "");
}

void sampleBattery(os::Ticks now)
{
    if (platform->battery == nullptr) {
        return;
    }
    const int levelBefore = battery.level();
    const bool lowBefore = battery.low();
    const bool validBefore = battery.valid();
    battery.update(platform->battery->readMillivolts(), now);
    if (!battery.valid()) {
        return;
    }
    if (!validBefore) {
        logBattery("");
    } else if (battery.low() != lowBefore) {
        if (battery.low()) {
            log::warning("battery") << "low, please charge: " << volts(battery.millivolts()).view();
        } else {
            logBattery("no longer low: ");
        }
    } else if (battery.level() != levelBefore) {
        logBattery("");
    }
    ui::post(ui::Battery{static_cast<std::uint16_t>(battery.millivolts()), static_cast<std::uint8_t>(battery.level()), battery.low()});
}

void systemMain(void *)
{
    log::info("system") << "DeltaMM " << Version << " on " << platform->boardName;
    if (platform->bootReport != nullptr) {
        log::info("system") << platform->bootReport;
    }
    logResetReport();
    platform->system.holdPower();
    loadCalibration();

    // A short red flash first shows the image runs, whatever happens next.
    platform->leds.set(Leds::Led::Red, true);
    for (os::Ticks t = 0; t < StartFlashMs && !os::stopRequested(); t += StepMs) {
        feedWatchdog();
        os::sleepMs(StepMs);
    }
    platform->leds.set(Leds::Led::Red, false);

    lastActivity.store(os::nowMs(), std::memory_order_relaxed);
    os::Ticks wake = os::nowMs();
    os::Ticks phase = 0;
    os::Ticks sinceReport = 0;
    os::Ticks sinceBattery = BatteryEveryMs;
    bool switchWasOff = false;
    bool emptyReported = false;
    while (!os::stopRequested()) {
        feedWatchdog();
        const os::Ticks now = os::nowMs();

        // Two readings 10 ms apart, so a contact bounce does not power down.
        const bool switchOff = platform->system.powerSwitchOff();
        if (switchOff && switchWasOff) {
            waitForSwitchOffThenPowerDown();
            return;
        }
        switchWasOff = switchOff;

        sinceBattery += StepMs;
        if (sinceBattery >= BatteryEveryMs) {
            sinceBattery = 0;
            sampleBattery(now);
            if (battery.empty() && !emptyReported) {
                emptyReported = true;
                if (batteryCalibrated) {
                    softOff("battery empty");
                    return;
                }
                log::warning("battery") << "below the empty level, but not switching off: no calibration yet";
            }
        }
        if (batteryReportWanted.exchange(false)) {
            if (battery.valid()) {
                logBattery("");
            } else {
                log::info("battery") << "no reading";
            }
        }

        const int minutes = autoOffMinutes.load(std::memory_order_relaxed);
        if (minutes > 0 && now - lastActivity.load(std::memory_order_relaxed) >= static_cast<os::Ticks>(minutes) * 60'000u) {
            softOff("auto power-off");
            return;
        }

        // Green heartbeat once a second; while the battery is low the red
        // LED blinks as well.
        platform->leds.set(Leds::Led::Green, phase < HeartbeatOnMs);
        platform->leds.set(Leds::Led::Red, battery.low() && now % (2 * LowBatteryBlinkMs) < LowBatteryBlinkMs);

        os::sleepUntil(wake, StepMs);
        phase = (phase + StepMs) % HeartbeatPeriodMs;
        sinceReport += StepMs;
        if (sinceReport >= UptimeReportMs) {
            sinceReport = 0;
            log::info("system") << "uptime " << (os::nowMs() / 1000) << " s, unused stack words: system "
                                << systemTask.unusedStackWords();
        }
    }
    platform->leds.set(Leds::Led::Green, false);
}

} // namespace

void start(Platform &p)
{
    platform = &p;
    systemTask.start("system", 1, systemMain, nullptr);
}

void noteActivity()
{
    services::power::activity();
    lastActivity.store(os::nowMs(), std::memory_order_relaxed);
}

void setAutoPowerOffMinutes(int minutes)
{
    autoOffMinutes.store(minutes < 0 ? 0 : minutes, std::memory_order_relaxed);
    noteActivity();
}

void stopFeedingWatchdog()
{
    feedingStopped.store(true, std::memory_order_relaxed);
}

void reportBattery()
{
    batteryReportWanted.store(true, std::memory_order_relaxed);
}

const services::StockCalibration *calibration()
{
    return calibrationReady.load(std::memory_order_acquire) ? &stockCalibration : nullptr;
}

} // namespace dmm::app::system
