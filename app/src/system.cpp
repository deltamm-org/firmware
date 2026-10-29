// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "system.h"

#include "radio.h"

#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/os/os.h"
#include "dmm/services/battery.h"
#include "dmm/services/clock.h"
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
constexpr os::Ticks UptimeReportMs = 10000;
constexpr os::Ticks BatteryEveryMs = 250;
constexpr os::Ticks LowBatteryBlinkMs = 800;
// Time for the link task to send the last log lines before power goes.
constexpr os::Ticks LogFlushMs = 200;

std::atomic<bool> feedingStopped{false};
std::atomic<os::Ticks> lastActivity{0};
std::atomic<int> autoOffMinutes{0};
std::atomic<bool> batteryReportWanted{false};

// The real-time clock: read by the system task only, published for others.
constexpr os::Ticks ClockEveryMs = 100;
std::atomic<bool> clockRunning{false};
std::atomic<std::uint32_t> clockSeconds{0};
std::atomic<bool> clockSetWanted{false};
std::atomic<std::uint32_t> clockSetTo{0};
std::atomic<const char *> clockSetSource{nullptr};
std::atomic<bool> clockReportWanted{false};

// The system task's battery monitor, with the board's levels; without a
// battery sense no level is ever low or empty.
services::BatteryMonitor &battery()
{
    static services::BatteryMonitor monitor(
        platform->battery != nullptr ? platform->battery->thresholds() : services::BatteryThresholds{});
    return monitor;
}
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
    log::info("battery") << what << volts(battery().millivolts()).view() << ", level " << battery().level()
                         << (battery().low() ? ", low" : "") << (batteryCalibrated ? "" : " (uncalibrated)");
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
    platform->leds.set(Leds::Led::Blue, false);
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
    platform->leds.set(Leds::Led::Blue, false);
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
    board::Calibration *cal = platform->calibration;
    if (cal == nullptr) {
        return;
    }
    if (!cal->load(*platform->storage)) {
        log::error("calibration") << "could not be read";
        return;
    }
    calibrationReady.store(cal->present(), std::memory_order_release);
    const auto offset = cal->batteryOffsetMv();
    if (!cal->present() || !offset) {
        log::warning("calibration") << cal->source() << " read, but the radio does not look calibrated";
        return;
    }
    battery().setOffset(*offset);
    batteryCalibrated = true;
    log::info("calibration") << cal->source() << ", battery offset " << (*offset < 0 ? "-" : "+")
                             << (*offset < 0 ? -*offset : *offset) << " mV";
}

void sampleBattery(os::Ticks now)
{
    if (platform->battery == nullptr) {
        return;
    }
    const int levelBefore = battery().level();
    const bool lowBefore = battery().low();
    const bool validBefore = battery().valid();
    battery().update(platform->battery->readMillivolts(), now);
    if (!battery().valid()) {
        return;
    }
    if (!validBefore) {
        logBattery("");
    } else if (battery().low() != lowBefore) {
        if (battery().low()) {
            log::warning("battery") << "low, please charge: " << volts(battery().millivolts()).view();
        } else {
            logBattery("no longer low: ");
        }
    } else if (battery().level() != levelBefore) {
        logBattery("");
    }
    ui::post(ui::Battery{static_cast<std::uint16_t>(battery().millivolts()), static_cast<std::uint8_t>(battery().level()), battery().low()});
}

// "2026-10-26 19:34:15"
lib::StaticString<20> dateTimeText(std::uint32_t seconds)
{
    const services::DateTime t = services::dateTime(seconds);
    lib::StaticString<20> s;
    s << t.year << '-' << lib::zeroPadded(t.month, 2) << '-' << lib::zeroPadded(t.day, 2) << ' ' << lib::zeroPadded(t.hour, 2)
      << ':' << lib::zeroPadded(t.minute, 2) << ':' << lib::zeroPadded(t.second, 2);
    return s;
}

void logClock(const char *what)
{
    auto line = log::info("clock");
    line << what;
    if (!clockRunning.load()) {
        line << "no clock";
    } else if (clockSeconds.load() < services::EarliestValidSeconds) {
        line << "time unknown";
    } else {
        line << dateTimeText(clockSeconds.load()).view() << " UTC";
    }
}

void startClock()
{
    if (platform->rtc == nullptr) {
        return;
    }
    if (!platform->rtc->start()) {
        log::warning("clock") << "the real-time clock did not start";
        return;
    }
    clockSeconds.store(platform->rtc->seconds());
    clockRunning.store(true);
    logClock("");
}

// Publishes the clock and applies a requested change.
void serviceClock()
{
    if (!clockRunning.load()) {
        return;
    }
    if (clockSetWanted.exchange(false)) {
        const std::uint32_t before = platform->rtc->seconds();
        const std::uint32_t to = clockSetTo.load();
        platform->rtc->setSeconds(to);
        clockSeconds.store(to);
        auto line = log::info("clock");
        line << "set from " << clockSetSource.load() << ": " << dateTimeText(to).view() << " UTC";
        if (before >= services::EarliestValidSeconds) {
            const auto off = static_cast<std::int32_t>(before - to);
            line << " (was " << (off < 0 ? -off : off) << " s " << (off < 0 ? "behind" : "ahead") << ")";
        } else {
            line << " (was unknown)";
        }
        return;
    }
    clockSeconds.store(platform->rtc->seconds());
    if (clockReportWanted.exchange(false)) {
        logClock("");
    }
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
    startClock();

    // A short red flash first shows the image runs, whatever happens next.
    platform->leds.set(Leds::Led::Red, true);
    for (os::Ticks t = 0; t < StartFlashMs && !os::stopRequested(); t += StepMs) {
        feedWatchdog();
        os::sleepMs(StepMs);
    }
    platform->leds.set(Leds::Led::Red, false);

    lastActivity.store(os::nowMs(), std::memory_order_relaxed);
    os::Ticks wake = os::nowMs();
    os::Ticks sinceReport = 0;
    os::Ticks sinceBattery = BatteryEveryMs;
    os::Ticks sinceClock = 0;
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

        sinceClock += StepMs;
        if (sinceClock >= ClockEveryMs || clockSetWanted.load()) {
            sinceClock = 0;
            serviceClock();
        }

        sinceBattery += StepMs;
        if (sinceBattery >= BatteryEveryMs) {
            sinceBattery = 0;
            sampleBattery(now);
            if (battery().empty() && !emptyReported) {
                emptyReported = true;
                if (batteryCalibrated) {
                    softOff("battery empty");
                    return;
                }
                log::warning("battery") << "below the empty level, but not switching off: no calibration yet";
            }
        }
        if (batteryReportWanted.exchange(false)) {
            if (battery().valid()) {
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

        // Green while FM is heard, blue while a DMR call is, red on the air;
        // while the battery is low the red LED blinks when not transmitting.
        const radio::Activity air = radio::activity();
        const bool lowBlink = battery().low() && now % (2 * LowBatteryBlinkMs) < LowBatteryBlinkMs;
        platform->leds.set(Leds::Led::Green, air == radio::Activity::FmReceive);
        platform->leds.set(Leds::Led::Blue, air == radio::Activity::DmrReceive);
        platform->leds.set(Leds::Led::Red, air == radio::Activity::Transmit || lowBlink);

        os::sleepUntil(wake, StepMs);
        sinceReport += StepMs;
        if (sinceReport >= UptimeReportMs) {
            sinceReport = 0;
            auto line = log::info("system");
            line << "uptime " << (os::nowMs() / 1000) << " s";
            for (std::size_t i = 0; os::taskStack(i).name != nullptr; ++i) {
                const os::TaskStack t = os::taskStack(i);
                line << (i == 0 ? ", unused stack words: " : ", ") << t.name << ' ' << t.unusedWords;
            }
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

os::Ticks idleForMs()
{
    return os::nowMs() - lastActivity.load(std::memory_order_relaxed);
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

std::optional<std::uint32_t> utcNow()
{
    const std::uint32_t s = clockSeconds.load();
    if (!clockRunning.load() || s < services::EarliestValidSeconds) {
        return std::nullopt;
    }
    return s;
}

void setClock(std::uint32_t utc, const char *source)
{
    clockSetTo.store(utc);
    clockSetSource.store(source);
    clockSetWanted.store(true);
}

void reportClock()
{
    clockReportWanted.store(true);
}

const board::Calibration *calibration()
{
    return calibrationReady.load(std::memory_order_acquire) ? platform->calibration : nullptr;
}

} // namespace dmm::app::system
