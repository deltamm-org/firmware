// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "gps.h"

#include "radio.h"
#include "system.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/clock.h"
#include "dmm/services/gps_schedule.h"
#include "dmm/services/log.h"
#include "dmm/services/nmea.h"

#include <cstdlib>
#include <optional>

namespace dmm::app::gps {

namespace {

namespace log = services::log;
using services::GpsFix;
using services::NmeaParser;

board::Gps *port = nullptr;
board::GpsReceiver receiver;
os::Task<640> gpsTask;
os::Queue<Message, 4> inbox;

constexpr os::Ticks PollMs = 50;
// Silence after power-on, or between sentences, that counts as no data.
constexpr os::Ticks SilentMs = 5000;
// How often the clock wants a fresh GPS time.
constexpr std::uint32_t ClockNeedMs = 60 * 60'000;
// The clock follows GPS time when it is this far off, at most this often.
constexpr std::int64_t ClockToleranceS = 2;
constexpr os::Ticks ClockSetEveryMs = 60'000;
// During a clock calibration: data is read this often, and a gap this long
// before a byte makes it the first of a second's sentences.
constexpr os::Ticks CalibrationPollMs = 5;
constexpr os::Ticks BurstGapMs = 200;

bool hasTime(const GpsFix &f)
{
    return f.valid && f.timeValid && f.dateValid;
}

std::uint32_t fixSeconds(const GpsFix &f)
{
    return services::secondsSince1970({f.year, f.month, f.day, f.hour, f.minute, f.second});
}

// Sets the clock from a fix's UTC time and date when it is off; true when
// it did.
bool followGpsTime(const GpsFix &f, os::Ticks &lastSet, bool &everSet)
{
    if (!hasTime(f)) {
        return false;
    }
    if (everSet && os::nowMs() - lastSet < ClockSetEveryMs) {
        return false;
    }
    const std::uint32_t utc = fixSeconds(f);
    const auto now = system::utcNow();
    if (now && std::abs(static_cast<std::int64_t>(*now) - utc) < ClockToleranceS) {
        return false;
    }
    system::setClock(utc, "GPS");
    lastSet = os::nowMs();
    everSet = true;
    return true;
}

const char *clientName(services::GpsSchedule::Client client)
{
    switch (client) {
    case services::GpsSchedule::Client::Clock:
        return "clock";
    case services::GpsSchedule::Client::Screen:
        return "screen";
    case services::GpsSchedule::Client::Console:
        return "console";
    case services::GpsSchedule::Client::Count:
        break;
    }
    return "?";
}

void logFix(const GpsFix &f, std::string_view what)
{
    auto line = log::info("gps");
    line << what;
    if (f.valid) {
        line << services::latitudeText(f.latitudeE7).view() << ", " << services::longitudeText(f.longitudeE7).view() << ", "
             << f.altitudeDm / 10 << " m, ";
    } else {
        line << "no fix, ";
    }
    line << f.satellitesUsed << " of " << f.satellitesInView << " satellites";
    if (f.timeValid) {
        line << ", " << lib::zeroPadded(f.hour, 2) << ':' << lib::zeroPadded(f.minute, 2) << ':' << lib::zeroPadded(f.second, 2)
             << " UTC";
    }
}

void gpsMain(void *)
{
    NmeaParser parser;
    services::GpsSchedule schedule;
    bool enabled = false;
    bool powered = false;
    bool raw = false;
    bool hadFix = false;
    bool fixChangePending = false;
    bool silentReported = false;
    std::uint32_t bytes = 0;
    std::uint32_t sentences = 0;
    os::Ticks lastData = 0;
    os::Ticks clockSetAt = 0;
    bool clockEverSet = false;
    std::uint8_t systems = services::gps_systems::Gps | services::gps_systems::Beidou;
    // The receiver is set up once it answers after power-on.
    bool configured = false;
    // A clock calibration run, and the clock's reading when the current
    // second's first sentence began.
    bool calibrating = false;
    std::uint8_t calibrationMinutes = 0;
    std::uint8_t calibrationReported = 0;
    os::Ticks calibrationStart = 0;
    services::ClockRateFit calibrationFit;
    std::optional<std::int64_t> burstAtMs;
    lib::StaticString<NmeaParser::MaxSentence> lastText;

    schedule.setNeed(services::GpsSchedule::Client::Clock, ClockNeedMs);

    auto write = [&](std::span<const std::uint8_t> data) { port->write(data); };
    auto configure = [&] {
        receiver.configure(systems, write);
        configured = true;
        log::info("gps") << "receiver set up: " << services::gps_systems::name(systems);
    };

    auto tellUi = [&] {
        ui::post(ui::Gps{parser.fix(), enabled, powered && !silentReported, schedule.asleep(powered)});
    };

    auto powerUp = [&] {
        radio::post(radio::GpsPower{true});
        port->start();
        powered = true;
        parser = NmeaParser();
        hadFix = false;
        fixChangePending = false;
        silentReported = false;
        configured = false;
        lastText.clear();
        lastData = os::nowMs();
    };
    auto powerDown = [&] {
        port->stop();
        radio::post(radio::GpsPower{false});
        powered = false;
    };
    auto tellCalibration = [&](ui::ClockCalibration::State state) {
        ui::post(ui::ClockCalibration{state, calibrationReported, calibrationMinutes});
    };
    auto startCalibration = [&](std::uint8_t minutes) {
        if (!enabled) {
            log::info("clock") << "calibration needs the GPS on";
            tellCalibration(ui::ClockCalibration::State::GpsOff);
            return;
        }
        calibrating = true;
        calibrationMinutes = minutes;
        calibrationReported = 0;
        calibrationStart = os::nowMs();
        calibrationFit.clear();
        schedule.setNeed(services::GpsSchedule::Client::Clock, 1000);
        log::info("clock") << "calibrating against GPS for " << minutes << " min";
        tellCalibration(ui::ClockCalibration::State::Running);
    };
    // Ends a run; with enough samples, corrects the clock's rate by what
    // the run measured.
    auto endCalibration = [&](bool complete) {
        calibrating = false;
        schedule.setNeed(services::GpsSchedule::Client::Clock, ClockNeedMs);
        // The next fix sets the clock if it is off.
        clockEverSet = false;
        if (!complete) {
            log::info("clock") << "calibration stopped";
            tellCalibration(ui::ClockCalibration::State::Stopped);
            return;
        }
        const std::uint32_t wanted = calibrationMinutes * 30u;
        if (calibrationFit.count() < wanted) {
            log::info("clock") << "calibration: " << calibrationFit.count() << " seconds with a fix, " << wanted
                               << " needed; rate unchanged";
            tellCalibration(ui::ClockCalibration::State::TooFewFixes);
            return;
        }
        const std::int32_t rate = calibrationFit.ratePpb();
        log::info("clock") << "calibration: the clock ran " << system::ppmText(rate).view() << " ("
                           << system::perDayText(rate).view() << " a day) over " << calibrationFit.count()
                           << " seconds, scatter " << calibrationFit.scatterMs() << " ms";
        system::setClockRate(system::clockRate() - rate);
        tellCalibration(ui::ClockCalibration::State::Done);
    };

    // Carries out what the schedule decides, and says why in the log.
    auto followSchedule = [&] {
        const os::Ticks now = os::nowMs();
        switch (schedule.update(now, powered)) {
        case services::GpsSchedule::Action::PowerOn:
            powerUp();
            log::info("gps") << "receiver on" << (schedule.continuous() ? "" : " for a fix");
            tellUi();
            break;
        case services::GpsSchedule::Action::PowerOff:
            powerDown();
            if (enabled) {
                log::info("gps") << "receiver asleep; wakes in " << (schedule.wakeAt() - now) / 1000 << " s";
            } else {
                log::info("gps") << "receiver off";
            }
            tellUi();
            break;
        case services::GpsSchedule::Action::None:
            break;
        }
    };

    while (!os::stopRequested()) {
        Message message;
        if (inbox.receive(message, calibrating ? CalibrationPollMs : PollMs)) {
            lib::visit(lib::Overload{
                           [&](const Power &p) {
                               if (p.on == enabled) {
                                   return;
                               }
                               enabled = p.on;
                               schedule.setEnabled(enabled);
                               log::info("gps") << (enabled ? "on" : "off");
                               tellUi();
                           },
                           [&](const PowerSave &p) {
                               schedule.setPowerSave(p.on);
                               log::info("gps") << "power saving " << (p.on ? "on" : "off");
                           },
                           [&](const Need &n) {
                               schedule.setNeed(n.client, n.intervalMs);
                               auto line = log::info("gps");
                               line << clientName(n.client) << " needs ";
                               if (n.intervalMs == 0) {
                                   line << "no position";
                               } else {
                                   line << "a position every " << n.intervalMs / 1000 << " s";
                               }
                           },
                           [&](const Report &) {
                               log::info("gps") << (enabled ? "on" : "off") << ", " << bytes << " bytes, " << sentences
                                                << " sentences, " << parser.badChecksums() << " bad, " << port->lostBytes()
                                                << " lost";
                               logFix(parser.fix(), "");
                               log::info("gps") << "systems " << services::gps_systems::name(systems)
                                                << (configured ? "" : " (receiver not set up yet)");
                               auto line = log::info("gps");
                               line << "receiver " << (powered ? "awake" : enabled ? "asleep"
                                                                                   : "off");
                               if (enabled) {
                                   line << (schedule.continuous() ? ", kept on" : ", sleeps between fixes");
                                   if (!powered && schedule.shortestNeed() != 0) {
                                       line << ", wakes in " << (schedule.wakeAt() - os::nowMs()) / 1000 << " s";
                                   }
                                   line << "; shortest need " << schedule.shortestNeed() / 1000 << " s, start lead "
                                        << schedule.leadMs() / 1000 << " s";
                               }
                           },
                           [&](const Raw &r) {
                               raw = r.on;
                               log::info("gps") << "raw sentences " << (raw ? "on" : "off");
                           },
                           [&](const Systems &s) {
                               systems = s.systems;
                               if (configured) {
                                   configure();
                               } else {
                                   log::info("gps") << "systems " << services::gps_systems::name(systems);
                               }
                           },
                           [&](const CalibrateClock &c) {
                               if (calibrating) {
                                   endCalibration(false);
                               }
                               if (c.minutes != 0) {
                                   startCalibration(c.minutes);
                               }
                           },
                           [&](const ChipCommand &c) {
                               if (!powered) {
                                   log::info("gps") << "the receiver is off";
                               } else if (!receiver.debugCommand(c.text.view(), write)) {
                                   log::info("gps") << "unknown command, try help";
                               }
                           },
                       },
                message);
        }
        if (calibrating && !enabled) {
            endCalibration(false);
        }
        if (calibrating) {
            const auto done = static_cast<std::uint8_t>((os::nowMs() - calibrationStart) / 60'000);
            if (done >= calibrationMinutes) {
                calibrationReported = calibrationMinutes;
                endCalibration(true);
            } else if (done != calibrationReported) {
                calibrationReported = done;
                tellCalibration(ui::ClockCalibration::State::Running);
            }
        }
        followSchedule();
        if (!powered) {
            continue;
        }
        std::uint8_t buffer[128];
        for (;;) {
            const std::size_t n = port->read(buffer);
            if (n == 0) {
                break;
            }
            bytes += static_cast<std::uint32_t>(n);
            if (os::nowMs() - lastData >= BurstGapMs) {
                burstAtMs = system::utcNowMs();
            }
            lastData = os::nowMs();
            for (std::size_t i = 0; i < n; ++i) {
                const NmeaParser::Sentence s = parser.feed(static_cast<char>(buffer[i]));
                if (s == NmeaParser::Sentence::None) {
                    continue;
                }
                if (s != NmeaParser::Sentence::BadChecksum) {
                    ++sentences;
                }
                if (s != NmeaParser::Sentence::BadChecksum && !configured) {
                    configure();
                }
                if (raw) {
                    log::info("gps") << parser.lastSentence();
                } else if (s == NmeaParser::Sentence::Txt && parser.lastSentence() != lastText.view()) {
                    // Answers to commands, and status messages; a repeat is not logged again.
                    lastText.clear();
                    lastText << parser.lastSentence();
                    log::info("gps") << parser.lastSentence();
                }
                if (s != NmeaParser::Sentence::Rmc && s != NmeaParser::Sentence::Gga) {
                    continue;
                }
                if (silentReported) {
                    silentReported = false;
                    log::info("gps") << "receiver sending again";
                }
                // A change of fix shows in RMC; it is logged once the GGA
                // with the same epoch's altitude and satellites has come too.
                if (s == NmeaParser::Sentence::Gga && fixChangePending) {
                    fixChangePending = false;
                    logFix(parser.fix(), hadFix ? "fix: " : "fix lost: ");
                }
                if (s == NmeaParser::Sentence::Rmc && parser.fix().valid != hadFix) {
                    hadFix = parser.fix().valid;
                    fixChangePending = true;
                }
                if (s == NmeaParser::Sentence::Rmc) {
                    // A run keeps the clock as it is once it has samples.
                    const bool set = (!calibrating || calibrationFit.count() == 0) &&
                        followGpsTime(parser.fix(), clockSetAt, clockEverSet);
                    if (calibrating && !set && burstAtMs && hasTime(parser.fix())) {
                        const std::int64_t gpsMs = static_cast<std::int64_t>(fixSeconds(parser.fix())) * 1000;
                        calibrationFit.add(gpsMs, static_cast<std::int32_t>(*burstAtMs - gpsMs));
                    }
                    burstAtMs.reset();
                }
                // A fix counts once the GGA of the same second is in, so the
                // receiver is not switched off before the fix is logged.
                if (s == NmeaParser::Sentence::Gga && parser.fix().valid) {
                    schedule.fix(os::nowMs());
                }
                tellUi();
            }
        }
        if (!silentReported && os::nowMs() - lastData >= SilentMs) {
            silentReported = true;
            log::warning("gps") << "no data from the receiver for " << SilentMs / 1000 << " s (" << bytes << " bytes so far)";
            tellUi();
        }
    }
}

} // namespace

void start(board::Gps &p)
{
    port = &p;
    gpsTask.start("gps", 3, gpsMain, nullptr);
}

const char *debugHelp()
{
    return receiver.debugHelp();
}

bool post(const Message &message)
{
    if (port == nullptr) {
        return false;
    }
    return inbox.send(message);
}

} // namespace dmm::app::gps
