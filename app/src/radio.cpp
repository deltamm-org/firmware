// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "radio.h"

#include "audio.h"
#include "system.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/battery_saver.h"
#include "dmm/services/log.h"
#include "dmm/services/power.h"
#include "dmm/services/squelch_gate.h"

#include <span>
#include <string_view>

namespace dmm::app::radio {

namespace {

namespace log = services::log;
namespace power = services::power;

services::Baseband *baseband = nullptr;
services::FmTransceiver *fm = nullptr;
os::Task<512> radioTask;
os::Queue<Message, 8> inbox;
// The last speaker volume asked for, applied again after every start.
int speakerVolume = -1;

// Half of full scale: loud enough to hear, short of clipping.
constexpr std::uint8_t ChipToneAmplitude = 0x20;

// FM receive: the channel, the squelch level and the speaker gate.
constexpr os::Ticks FmPollMs = 10;
constexpr os::Ticks IdleWaitMs = 1000;
constexpr int DefaultSquelchLevel = 3;
services::FmChannel fmChannel;
int squelchLevel = DefaultSquelchLevel;
services::SquelchGate gate;
bool speakerOpen = false;
services::BatterySaver saver;

// FM transmit settings and state.
constexpr std::uint16_t DefaultTimeoutS = 30;
std::uint32_t transmitHz = 0; // 0 = the receive frequency
std::uint16_t ctcssDeciHz = 0;
services::PowerLevel powerLevel = services::PowerLevel::Low;
std::uint16_t timeoutS = DefaultTimeoutS;
bool receiveOnly = false;
bool transmitting = false;
os::Ticks transmitStarted = 0;

// Transmit only inside the 2 m and 70 cm amateur bands.
struct Band
{
    std::uint32_t lowHz;
    std::uint32_t highHz;
};
constexpr Band TransmitBands[] = {{144'000'000, 146'000'000}, {430'000'000, 440'000'000}};

bool transmitAllowed(std::uint32_t hz)
{
    for (const Band &b : TransmitBands) {
        if (hz >= b.lowHz && hz <= b.highHz) {
            return true;
        }
    }
    return false;
}

const char *powerName(services::PowerLevel level)
{
    constexpr const char *Names[] = {"low", "mid", "high", "turbo"};
    return Names[static_cast<int>(level) & 3];
}

// The squelch gate's result: the baseband passes receive audio only while
// told a carrier is present, and the speaker opens for it.
void setSpeaker(bool open)
{
    if (open == speakerOpen) {
        return;
    }
    speakerOpen = open;
    if (open) {
        power::hold(power::AwakeReason::Carrier);
    } else {
        power::release(power::AwakeReason::Carrier);
    }
    ui::post(ui::Receiving{open});
    baseband->setCarrier(open);
    audio::post(audio::ReceiveAudio{open});
}

// The baseband's analog FM mode for the current channel, receiving.
void basebandAnalogReceive(const services::StockCalibration &cal)
{
    const auto gains = cal.basebandAnalogGains();
    const bool ok = gains && baseband->setAnalog(fmChannel.wide, *gains) &&
        baseband->setWorkMode(services::Baseband::WorkMode::Receive) && baseband->setCarrier(speakerOpen);
    if (!ok) {
        log::warning("baseband") << "analog receive set-up not accepted";
    }
}

// MHz with three decimals, for the log.
lib::StaticString<12> mhz(std::uint32_t hz)
{
    const std::uint32_t khz = hz / 1000;
    lib::StaticString<12> text;
    text << khz / 1000 << '.' << static_cast<char>('0' + khz / 100 % 10) << static_cast<char>('0' + khz / 10 % 10)
         << static_cast<char>('0' + khz % 10);
    return text;
}

void report()
{
    const services::BasebandInfo i = baseband->info();
    if (!i.running) {
        log::info("baseband") << "not running: " << (i.problem != nullptr ? i.problem : "not started")
                              << "; data line " << (i.dataWaiting ? "waiting" : "idle");
        return;
    }
    log::info("baseband") << "running: product " << i.product << ", hardware " << i.hardware << ", software " << i.software
                          << "; data line " << (i.dataWaiting ? "waiting" : "idle") << "; unused stack words: radio "
                          << radioTask.unusedStackWords();
}

// One received packet as hex, 24 bytes per line.
void logPacket(std::uint8_t type, std::span<const std::uint8_t> fields)
{
    log::info("baseband") << "reply type " << lib::hex(type, 2) << ", " << fields.size() << " bytes";
    for (std::size_t at = 0; at < fields.size(); at += 24) {
        char text[24 * 3 + 1];
        std::size_t n = 0;
        for (std::size_t i = at; i < fields.size() && i < at + 24; ++i) {
            constexpr char Digits[] = "0123456789abcdef";
            text[n++] = Digits[fields[i] >> 4];
            text[n++] = Digits[fields[i] & 0xF];
            text[n++] = ' ';
        }
        text[n] = '\0';
        log::info("baseband") << "  " << text;
    }
}

// Resets the FM transceiver: idle, microphone muted (it reaches the
// speaker otherwise), every RF switch off.
void startFm()
{
    if (fm == nullptr) {
        return;
    }
    const std::uint16_t before = fm->readRegister(0x1F);
    if (!fm->start()) {
        log::error("fm") << "no answer from the FM transceiver (id " << lib::hex(fm->readRegister(0), 4) << ")";
        return;
    }
    log::info("fm") << "transceiver found; microphone muted, idle (pins " << lib::hex(before, 4) << " -> "
                    << lib::hex(fm->readRegister(0x1F), 4) << ")";
}

void fmReceive()
{
    const services::StockCalibration *cal = system::calibration();
    if (cal == nullptr) {
        log::error("fm") << "receive refused: no calibration loaded";
        return;
    }
    const int level = squelchLevel < 1 ? 1 : squelchLevel;
    const char *problem = fm->receive(fmChannel, level, *cal);
    saver.reset();
    gate.reset();
    gate.setForcedOpen(squelchLevel == 0);
    if (problem != nullptr) {
        setSpeaker(false);
        log::error("fm") << "receive refused at " << mhz(fmChannel.receiveHz).view() << " MHz: " << problem;
        return;
    }
    basebandAnalogReceive(*cal);
    const auto threshold = cal->squelchThreshold(fmChannel.receiveHz, level);
    log::info("fm") << "receiving " << mhz(fmChannel.receiveHz).view() << " MHz, " << (fmChannel.wide ? "25" : "12.5")
                    << " kHz, squelch " << squelchLevel << " (opens at -" << (threshold ? *threshold : 0)
                    << " dBm and noise " << services::squelchOpenNoiseDb(fmChannel.wide, level) << " dB)";
    setSpeaker(gate.open());
}

void fmStatus()
{
    if (!fm->receiving()) {
        log::info("fm") << "idle";
        return;
    }
    const services::FmReceiveStatus s = fm->status();
    log::info("fm") << mhz(fmChannel.receiveHz).view() << " MHz, RSSI " << s.rssiDbm << " dBm, noise " << s.noiseDb
                    << " dB, quality " << s.quality << ", carrier " << (s.carrier ? "yes" : "no") << ", speaker "
                    << (speakerOpen ? "open" : "closed");
}

// Mean signal strength over a few reads, after the receiver has settled.
int settledRssi()
{
    constexpr int Reads = 4;
    os::sleepMs(25);
    int sum = 0;
    for (int i = 0; i < Reads; ++i) {
        sum += fm->status().rssiDbm;
        os::sleepMs(5);
    }
    return sum / Reads;
}

// Front-end tuning against signal strength: a coarse pass over the whole
// range, then a fine pass around the strongest point.
void fmSweep()
{
    const services::StockCalibration *cal = system::calibration();
    if (!fm->receiving() || cal == nullptr) {
        log::info("fm") << "sweep: not receiving";
        return;
    }
    constexpr int Max = 0xFFF;
    const auto calibrated = cal->rxTune(fmChannel.receiveHz);
    log::info("fm") << "sweep at " << mhz(fmChannel.receiveHz).view() << " MHz, calibrated tune "
                    << (calibrated ? *calibrated : 0);
    int best = 0;
    int bestRssi = -200;
    auto measure = [&](int v) {
        fm->setReceiveTune(static_cast<std::uint16_t>(v));
        const int rssi = settledRssi();
        log::info("fm") << "tune " << v << ": " << rssi << " dBm";
        if (rssi > bestRssi) {
            bestRssi = rssi;
            best = v;
        }
    };
    for (int v = 0; v <= Max; v += 128) {
        measure(v);
    }
    measure(Max);
    const int from = best - 112 < 0 ? 0 : best - 112;
    const int to = best + 112 > Max ? Max : best + 112;
    for (int v = from; v <= to; v += 16) {
        measure(v);
    }
    fm->setReceiveTune(calibrated ? *calibrated : 0);
    const int atCalibrated = settledRssi();
    log::info("fm") << "sweep: strongest " << bestRssi << " dBm at tune " << best << "; calibrated "
                    << (calibrated ? *calibrated : 0) << " gives " << atCalibrated << " dBm (put back)";
}

// One squelch poll while receiving.
void pollFm()
{
    const services::FmReceiveStatus s = fm->status();
    const bool open = gate.update(s.carrier);
    if (open != speakerOpen) {
        log::info("fm") << "squelch " << (open ? "open" : "closed") << ", RSSI " << s.rssiDbm << " dBm, noise " << s.noiseDb
                        << " dB, quality " << s.quality;
        setSpeaker(open);
    }
}

lib::StaticString<16> ctcssText()
{
    lib::StaticString<16> text;
    if (ctcssDeciHz == 0) {
        text << "no tone";
    } else {
        text << "tone " << ctcssDeciHz / 10 << '.' << static_cast<char>('0' + ctcssDeciHz % 10) << " Hz";
    }
    return text;
}

void startTransmit()
{
    if (transmitting) {
        return;
    }
    const services::StockCalibration *cal = system::calibration();
    if (fm == nullptr || !fm->receiving() || cal == nullptr) {
        log::info("fm") << "PTT: no FM channel set (fm rx first)";
        return;
    }
    services::FmChannel channel = fmChannel;
    channel.transmitHz = transmitHz != 0 ? transmitHz : fmChannel.receiveHz;
    if (receiveOnly) {
        log::info("fm") << "PTT: this channel is receive only";
        return;
    }
    if (!transmitAllowed(channel.transmitHz)) {
        log::warning("fm") << "PTT: " << mhz(channel.transmitHz).view() << " MHz is outside the 2 m and 70 cm bands";
        return;
    }
    setSpeaker(false);
    baseband->setMicrophone(true);
    baseband->setWorkMode(services::Baseband::WorkMode::Transmit);
    const char *problem = fm->transmit(channel, powerLevel, static_cast<std::uint16_t>(ctcssDeciHz * 10), *cal);
    if (problem != nullptr) {
        fm->endTransmit();
        baseband->setMicrophone(false);
        log::error("fm") << "transmit refused at " << mhz(channel.transmitHz).view() << " MHz: " << problem;
        fmReceive();
        return;
    }
    transmitting = true;
    power::hold(power::AwakeReason::Transmit);
    ui::post(ui::Transmitting{true});
    transmitStarted = os::nowMs();
    log::info("fm") << "transmitting " << mhz(channel.transmitHz).view() << " MHz, power " << powerName(powerLevel)
                    << " (control " << fm->transmitPower() << "), " << ctcssText().view() << ", timeout " << timeoutS << " s";
}

void stopTransmit(const char *why)
{
    if (!transmitting) {
        return;
    }
    fm->endTransmit();
    baseband->setMicrophone(false);
    transmitting = false;
    power::release(power::AwakeReason::Transmit);
    ui::post(ui::Transmitting{false});
    log::info("fm") << "transmit off (" << why << ") after " << (os::nowMs() - transmitStarted) / 100 / 10 << "."
                    << (os::nowMs() - transmitStarted) / 100 % 10 << " s";
    fmReceive();
}

bool haveFm()
{
    if (fm == nullptr) {
        log::info("fm") << "no FM transceiver";
    }
    return fm != nullptr;
}

void bringUp()
{
    log::info("baseband") << "starting";
    const os::Ticks started = os::nowMs();
    baseband->start();
    log::info("baseband") << "start took " << (os::nowMs() - started) << " ms";
    report();
    if (speakerVolume >= 0) {
        baseband->setSpeakerVolume(speakerVolume);
    }
    // A restart forgets the analog set-up and the codec trims.
    const services::StockCalibration *cal = system::calibration();
    if (cal != nullptr) {
        if (const auto t = cal->codecTrims(); !t || !baseband->setCodecTrims(t->leftOut, t->rightOut, t->rightInput)) {
            log::warning("baseband") << "codec trims not applied";
        }
    }
    if (fm != nullptr && fm->receiving() && cal != nullptr) {
        basebandAnalogReceive(*cal);
    }
}

const char *saveModeText(std::uint8_t mode)
{
    constexpr const char *Texts[] = {"off", "on, 150 ms sleep", "on, 70 ms sleep"};
    return Texts[mode <= 2 ? mode : 1];
}

void saveStatus()
{
    lib::StaticString<64> reasons;
    for (unsigned i = 0; i < static_cast<unsigned>(power::AwakeReason::Count); ++i) {
        const auto reason = static_cast<power::AwakeReason>(i);
        if (power::isHeld(reason)) {
            reasons << (reasons.view().empty() ? "" : ", ") << power::name(reason);
        }
    }
    log::info("power") << "battery save " << saveModeText(saver.mode()) << "; "
                       << (saver.active() ? (saver.asleep() ? "receiver asleep" : "listening") : "awake") << ", "
                       << saver.cycles() << " sleeps so far; awake for: "
                       << (reasons.view().empty() ? std::string_view("nothing") : reasons.view()) << ", idle "
                       << power::idleForMs() / 1000 << " s";
}

// Sleeps or wakes the receiver as battery save asks.
void saveStep()
{
    const bool receiverOn = fm != nullptr && !transmitting && (fm->receiving() || fm->receiverAsleep());
    const bool wasActive = saver.active();
    switch (saver.update(os::nowMs(), power::idleForMs(), receiverOn)) {
    case services::BatterySaver::Action::Sleep:
        fm->sleepReceiver();
        break;
    case services::BatterySaver::Action::Wake:
        fm->wakeReceiver();
        break;
    case services::BatterySaver::Action::None:
        break;
    }
    if (saver.active() != wasActive) {
        if (saver.active()) {
            log::info("power") << "battery save: receiver sleeps " << services::BatterySaver::offMs(saver.mode())
                               << " ms, listens " << services::BatterySaver::ListenMs << " ms";
        } else {
            log::info("power") << "battery save ended after " << saver.cycles() << " sleeps so far";
        }
    }
}

void handle(Message &message)
{
    // Every command finds the receiver awake; battery save's own messages
    // leave it as it is.
    const bool saveMessage =
        std::get_if<BatterySave>(&message) != nullptr || std::get_if<BatterySaveStatus>(&message) != nullptr;
    if (saver.active() && !saveMessage) {
        if (fm != nullptr && fm->receiverAsleep()) {
            fm->wakeReceiver();
        }
        saver.reset();
        log::info("power") << "battery save ended after " << saver.cycles() << " sleeps so far";
    }
    // Anything but PTT itself ends a transmission first.
    if (transmitting && std::get_if<Ptt>(&message) == nullptr) {
        stopTransmit("another command");
    }
    lib::visit(lib::Overload{
                   [](const Info &) { report(); },
                   [](const Restart &) { bringUp(); },
                   [](const ChipTone &t) {
                       const bool ok = baseband->playTone(t.hz, t.ms, ChipToneAmplitude);
                       log::info("baseband") << "tone " << t.hz << " Hz, " << t.ms << " ms: " << (ok ? "accepted" : "refused");
                   },
                   [](const SpeakerVolume &v) {
                       speakerVolume = v.step;
                       const bool ok = baseband->setSpeakerVolume(v.step);
                       log::info("baseband") << "speaker volume " << v.step << ": " << (ok ? "set" : "refused");
                   },
                   [](const CodecWrite &c) {
                       const bool ok = baseband->codecWrite(c.page, c.reg, c.value);
                       log::info("baseband") << "codec page " << c.page << " register " << lib::hex(c.reg, 2) << " = "
                                             << lib::hex(c.value, 2) << ": " << (ok ? "accepted" : "refused");
                   },
                   [](const RawPacket &p) {
                       log::info("baseband") << "sending type " << lib::hex(p.type, 2) << ", " << p.count << " field bytes";
                       int replies = 0;
                       baseband->exchange(p.type, {p.fields, p.count}, 1000,
                           [&replies](std::uint8_t type, std::span<const std::uint8_t> fields) {
                               ++replies;
                               logPacket(type, fields);
                           });
                       if (replies == 0) {
                           log::info("baseband") << "no reply within 1 s";
                       }
                   },
                   [](const FmRead &r) {
                       if (fm == nullptr) {
                           log::info("fm") << "no FM transceiver";
                           return;
                       }
                       log::info("fm") << "register " << lib::hex(r.reg, 2) << " = " << lib::hex(fm->readRegister(r.reg), 4);
                   },
                   [](const FmWrite &w) {
                       if (haveFm()) {
                           fm->writeRegister(w.reg, w.value);
                           log::info("fm") << "register " << lib::hex(w.reg, 2) << " = " << lib::hex(w.value, 4)
                                           << ", reads back " << lib::hex(fm->readRegister(w.reg), 4);
                       }
                   },
                   [](const FmReceive &r) {
                       if (haveFm()) {
                           fmChannel = services::FmChannel{r.receiveHz, r.receiveHz, r.wide};
                           fmReceive();
                       }
                   },
                   [](const FmIdle &) {
                       if (haveFm()) {
                           fm->idle();
                           setSpeaker(false);
                           baseband->setWorkMode(services::Baseband::WorkMode::Idle);
                           log::info("fm") << "idle";
                       }
                   },
                   [](const FmSquelch &s) {
                       squelchLevel = s.level;
                       if (haveFm() && fm->receiving()) {
                           fmReceive();
                       } else {
                           log::info("fm") << "squelch " << squelchLevel;
                       }
                   },
                   [](const FmStatus &) {
                       if (haveFm()) {
                           fmStatus();
                       }
                   },
                   [](const FmSweep &) {
                       if (haveFm()) {
                           fmSweep();
                       }
                   },
                   [](const Ptt &p) {
                       if (p.down) {
                           startTransmit();
                       } else {
                           stopTransmit("PTT released");
                       }
                   },
                   [](const FmTransmitFrequency &f) {
                       transmitHz = f.hz;
                       log::info("fm") << "transmit on "
                                       << (f.hz != 0 ? mhz(f.hz).view() : std::string_view("the receive frequency"))
                                       << (f.hz != 0 ? " MHz" : "");
                   },
                   [](const FmCtcss &c) {
                       ctcssDeciHz = c.deciHz;
                       log::info("fm") << "transmit " << ctcssText().view();
                   },
                   [](const FmPower &p) {
                       powerLevel = p.level;
                       log::info("fm") << "transmit power " << powerName(powerLevel);
                   },
                   [](const FmTimeout &t) {
                       timeoutS = t.seconds;
                       log::info("fm") << "transmit timeout " << timeoutS << " s";
                   },
                   [](const UseChannel &u) {
                       if (!haveFm()) {
                           return;
                       }
                       const services::Channel &c = u.channel;
                       if (!c.analog()) {
                           fm->idle();
                           setSpeaker(false);
                           log::info("fm") << "digital channel " << c.name << ": not supported yet, receiver off";
                           return;
                       }
                       fmChannel = services::FmChannel{c.receiveHz, c.transmitHz, c.wide};
                       transmitHz = c.transmitHz;
                       ctcssDeciHz = c.transmitTone.kind == services::Tone::Kind::Ctcss ? c.transmitTone.value : 0;
                       if (c.transmitTone.kind == services::Tone::Kind::Dcs) {
                           log::warning("fm") << "DCS transmit not supported yet: sending no tone";
                       }
                       powerLevel = c.power;
                       receiveOnly = c.receiveOnly;
                       squelchLevel = u.squelch;
                       fmReceive();
                   },
                   [](const FmTune &t) {
                       const services::StockCalibration *cal = system::calibration();
                       if (!haveFm() || !fm->receiving() || cal == nullptr) {
                           log::info("fm") << "tune: not receiving";
                           return;
                       }
                       const auto calibrated = cal->rxTune(fmChannel.receiveHz);
                       const int v = t.value >= 0 ? t.value : (calibrated ? *calibrated : 0);
                       fm->setReceiveTune(static_cast<std::uint16_t>(v));
                       log::info("fm") << "tune " << v << (t.value >= 0 ? " (held)" : " (calibrated)") << ": "
                                       << settledRssi() << " dBm";
                   },
                   [](const BatterySave &s) {
                       saver.setMode(s.mode);
                       log::info("power") << "battery save " << saveModeText(saver.mode());
                   },
                   [](const BatterySaveStatus &) { saveStatus(); },
                   [](const Microphone &m) {
                       if (fm == nullptr) {
                           log::info("fm") << "no FM transceiver";
                           return;
                       }
                       const std::uint16_t pins = fm->setMicrophoneMuted(!m.on);
                       log::info("fm") << "microphone " << (m.on ? "on" : "muted") << " (pins " << lib::hex(pins, 4) << ")";
                   },
               },
        message);
}

void radioMain(void *)
{
    startFm();
    bringUp();
    os::Ticks lastPoll = os::nowMs();
    while (!os::stopRequested()) {
        const bool busy = fm != nullptr && (fm->receiving() || transmitting);
        os::Ticks wait = busy ? FmPollMs : IdleWaitMs;
        if (saver.asleep()) {
            const auto left = static_cast<std::int32_t>(saver.nextChangeMs() - os::nowMs());
            wait = left > 0 ? static_cast<os::Ticks>(left) : 0;
        }
        Message m;
        if (inbox.receive(m, wait)) {
            handle(m);
        }
        saveStep();
        if (transmitting && timeoutS != 0 && os::nowMs() - transmitStarted >= static_cast<os::Ticks>(timeoutS) * 1000) {
            stopTransmit("timeout");
        }
        if (fm != nullptr && fm->receiving() && os::nowMs() - lastPoll >= FmPollMs) {
            lastPoll = os::nowMs();
            pollFm();
        }
    }
}

} // namespace

void start(services::Baseband &b, services::FmTransceiver *f)
{
    baseband = &b;
    fm = f;
    radioTask.start("radio", 7, radioMain, nullptr);
}

bool post(const Message &message)
{
    return baseband != nullptr && inbox.send(message);
}

} // namespace dmm::app::radio
