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

board::Fm *fm = nullptr;
board::Baseband *baseband = nullptr;
// Room for the deepest path, a channel change down to a baseband packet,
// with an interrupt on top and a margin.
os::Task<768> radioTask;
os::Queue<Message, 8> inbox;
// The last speaker volume asked for, applied again after every start.
int speakerVolume = -1;

// Half of full scale: loud enough to hear, short of clipping.
constexpr int ChipTonePercent = 50;

// FM receive: the channel, the squelch level and the speaker gate. A
// channel with a receive tone opens the speaker on the tone alone, else
// on the carrier; squelch level 0 forces only the carrier open.
constexpr os::Ticks FmPollMs = 10;
constexpr os::Ticks IdleWaitMs = 1000;
constexpr int DefaultSquelchLevel = 3;
services::FmChannel fmChannel;
int squelchLevel = DefaultSquelchLevel;
services::SquelchGate carrierGate;
// Polls before a tone counts as heard, and as gone: DCS words come slowly
// and drop out briefly, so DCS waits longer before closing.
constexpr int ToneOpenPolls = 5;
constexpr int CtcssClosePolls = 15;
constexpr int DcsClosePolls = 90;
services::SquelchGate toneGate(ToneOpenPolls, CtcssClosePolls);
bool speakerOpen = false;

// DMR receive: the channel's settings, whether it works through a
// repeater, and the call being heard.
services::dmr::ChannelSettings dmrSettings;
bool dmrRepeater = false;
bool inCall = false;
os::Ticks callStarted = 0;
bool timingFound = false;
services::BatterySaver saver;

// FM transmit settings and state.
constexpr std::uint16_t DefaultTimeoutS = 30;
std::uint32_t transmitHz = 0; // 0 = the receive frequency
services::Tone transmitTone;
services::PowerLevel powerLevel = services::PowerLevel::Low;
std::uint16_t timeoutS = DefaultTimeoutS;
bool receiveOnly = false;
bool transmitting = false;
// FM transmit audio takes the transceiver's own microphone input; adding
// the route through the baseband gives a delayed second copy.
FmTransmitAudio::Route transmitAudio = FmTransmitAudio::Route::Direct;
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

const char *routeName(FmTransmitAudio::Route route)
{
    constexpr const char *Names[] = {"both routes", "through the baseband only", "straight to the transceiver only"};
    return Names[static_cast<int>(route) % 3];
}

const char *powerName(services::PowerLevel level)
{
    constexpr const char *Names[] = {"low", "mid", "high", "turbo"};
    return Names[static_cast<int>(level) & 3];
}

// The board's baseband, if it has one.
template <class F>
void withBaseband(F &&f)
{
    app::withBaseband(baseband, f);
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
    // On FM the baseband passes audio only while told of a carrier; on DMR
    // it does its own carrier detection.
    if (!fmChannel.digital) {
        withBaseband([open](auto &b) { b.setCarrier(open); });
    }
    audio::post(audio::ReceiveAudio{open});
}

// Log lines of the receive set-up are built in their own frames, kept out
// of the deep path down to the chips.
[[gnu::noinline, gnu::cold]] void logAnalogRefused()
{
    log::warning("baseband") << "analog receive set-up not accepted";
}

// The baseband's analog FM mode for the current channel, receiving.
void basebandAnalogReceive()
{
    withBaseband([](auto &b) {
        const bool ok = b.setAnalog(fmChannel.wide) && b.setWorkMode(services::BasebandWorkMode::Receive) &&
            b.setCarrier(speakerOpen);
        if (!ok) {
            logAnalogRefused();
        }
    });
}

// "CTCSS 88.5 Hz", "DCS 023N", "no tone"; for the log.
lib::StaticString<20> toneText(const services::Tone &tone)
{
    lib::StaticString<20> text;
    switch (tone.kind) {
    case services::Tone::Kind::None:
        text << "no tone";
        break;
    case services::Tone::Kind::Ctcss:
        text << "CTCSS " << tone.value / 10 << '.' << static_cast<char>('0' + tone.value % 10) << " Hz";
        break;
    case services::Tone::Kind::Dcs:
        text << "DCS " << static_cast<char>('0' + (tone.value >> 6 & 7)) << static_cast<char>('0' + (tone.value >> 3 & 7))
             << static_cast<char>('0' + (tone.value & 7)) << (tone.inverted ? 'I' : 'N');
        break;
    }
    return text;
}

bool toneSquelch()
{
    return fmChannel.receiveTone.kind != services::Tone::Kind::None;
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
    if constexpr (!HasBaseband) {
        log::info("baseband") << "none on this radio";
    }
    withBaseband([](auto &b) {
        const services::BasebandInfo i = b.info();
        if (!i.running) {
            log::info("baseband") << "not running: " << (i.problem != nullptr ? i.problem : "not started")
                                  << "; data line " << (i.dataWaiting ? "waiting" : "idle");
            return;
        }
        log::info("baseband") << "running: product " << i.product << ", hardware " << i.hardware << ", software "
                              << i.software << "; data line " << (i.dataWaiting ? "waiting" : "idle");
    });
}

// Resets the FM transceiver: idle, microphone muted (it reaches the
// speaker otherwise), every RF switch off.
void startFm()
{
    if (!fm->start()) {
        log::error("fm") << "no answer from the FM transceiver";
        return;
    }
    log::info("fm") << "transceiver found; microphone muted, idle";
}

[[gnu::noinline, gnu::cold]] void logFm(const char *what)
{
    log::error("fm") << what;
}

[[gnu::noinline, gnu::cold]] void logReceiveRefused(const char *problem)
{
    log::error("fm") << "receive refused at " << mhz(fmChannel.receiveHz).view() << " MHz: " << problem;
}

[[gnu::noinline]] void logReceiving()
{
    log::info("fm") << "receiving " << mhz(fmChannel.receiveHz).view() << " MHz, " << (fmChannel.wide ? "25" : "12.5")
                    << " kHz, squelch " << squelchLevel
                    << (toneSquelch() ? ", opens on " : ", opens on the carrier")
                    << (toneSquelch() ? toneText(fmChannel.receiveTone).view() : std::string_view());
}

// The speaker's gate for the current channel.
bool gateOpen()
{
    return toneSquelch() ? toneGate.open() : carrierGate.open();
}

void fmReceive()
{
    if (system::calibration() == nullptr) {
        logFm("receive refused: no calibration loaded");
        return;
    }
    const int level = squelchLevel < 1 ? 1 : squelchLevel;
    const char *problem = fm->receive(fmChannel, level);
    saver.reset();
    carrierGate.reset();
    carrierGate.setForcedOpen(squelchLevel == 0);
    toneGate.reset();
    const bool dcs = fmChannel.receiveTone.kind == services::Tone::Kind::Dcs;
    toneGate.setCounts(ToneOpenPolls, dcs ? DcsClosePolls : CtcssClosePolls);
    if (problem != nullptr) {
        setSpeaker(false);
        logReceiveRefused(problem);
        return;
    }
    basebandAnalogReceive();
    logReceiving();
    setSpeaker(gateOpen());
}

[[gnu::noinline]] void logCallEnd(const char *why, os::Ticks ms)
{
    log::info("dmr") << "call " << why << " after " << ms / 100 / 10 << "." << ms / 100 % 10 << " s";
}

[[gnu::noinline, gnu::cold]] void logDmr(const char *what)
{
    log::error("dmr") << what;
}

[[gnu::noinline]] void logDmrReceiving(bool ok)
{
    log::info("dmr") << "receiving " << mhz(fmChannel.receiveHz).view() << " MHz, colour code " << dmrSettings.colourCode
                     << (dmrRepeater ? ", slot " : ", both slots direct")
                     << (dmrRepeater ? static_cast<int>(dmrSettings.slot) : 0) << (ok ? "" : "; baseband refused");
}

void endCall(const char *why)
{
    if (!inCall) {
        return;
    }
    inCall = false;
    logCallEnd(why, os::nowMs() - callStarted);
    setSpeaker(false);
}

// Tunes to the DMR channel and lets the baseband listen.
void dmrReceive()
{
    endCall("left");
    setSpeaker(false);
    timingFound = false;
    if (system::calibration() == nullptr) {
        logDmr("receive refused: no calibration loaded");
        return;
    }
    const char *problem = fm->receive(fmChannel, 1);
    saver.reset();
    if (problem != nullptr) {
        logReceiveRefused(problem);
        return;
    }
    bool ok = !HasBaseband;
    withBaseband([&ok](auto &b) { ok = b.setDigital(dmrSettings, dmrRepeater); });
    logDmrReceiving(ok);
}

// Receive on the current channel, FM or DMR.
void receive()
{
    if (fmChannel.digital) {
        dmrReceive();
    } else {
        fmReceive();
    }
}

void onDmrEvent(const services::dmr::Event &e)
{
    using Kind = services::dmr::Event::Kind;
    using services::dmr::DataType;
    switch (e.kind) {
    case Kind::SlotFound:
        if (!timingFound && e.verified) {
            timingFound = true;
            log::info("dmr") << (e.baseStation ? "repeater" : "direct") << " timing found"
                             << (e.inboundBusy ? ", inbound busy" : "");
        }
        break;
    case Kind::Burst: {
        const bool lcBurst = e.voice || e.type == DataType::VoiceLcHeader || e.type == DataType::TerminatorWithLc;
        const auto lc = lcBurst ? services::dmr::FullLc::decode(e.payload()) : std::nullopt;
        if (!lc) {
            log::debug("dmr") << "burst type " << static_cast<int>(e.type) << ", colour code " << e.colourCode;
            break;
        }
        if (!e.voice && e.type == DataType::TerminatorWithLc) {
            endCall("ended");
            break;
        }
        if (!inCall) {
            inCall = true;
            callStarted = os::nowMs();
            log::info("dmr") << (lc->groupCall() ? "group call" : lc->privateCall() ? "private call"
                                                                                    : "call")
                             << " from "
                             << lc->source << " to " << lc->destination << ", colour code " << e.colourCode
                             << (lc->emergency() ? ", emergency" : "");
            setSpeaker(true);
        }
        break;
    }
    case Kind::SyncLost:
        timingFound = false;
        endCall("lost");
        break;
    case Kind::CarrierFound:
    case Kind::CarrierLost:
        log::debug("dmr") << (e.kind == Kind::CarrierFound ? "carrier found" : "carrier lost");
        break;
    case Kind::AllSent:
        break;
    }
}

// One pass over what the baseband heard.
void pollDmr()
{
    withBaseband([](auto &b) {
        services::dmr::Event e;
        for (int i = 0; i < 8 && b.nextEvent(e); ++i) {
            onDmrEvent(e);
        }
    });
    if (inCall) {
        power::activity();
    }
}

void fmStatus()
{
    if (!fm->receiving()) {
        log::info("fm") << "idle";
        return;
    }
    const services::FmReceiveStatus s = fm->status();
    lib::StaticString<96> what;
    what << mhz(fmChannel.receiveHz).view() << " MHz, carrier " << (s.carrier ? "yes" : "no") << ", tone "
         << (!toneSquelch() ? "not needed" : s.toneHeard ? "heard"
                                                         : "not heard")
         << ", speaker "
         << (speakerOpen ? "open" : "closed");
    fm->logStatus(what.view());
}

// One squelch poll while receiving.
void pollFm()
{
    const services::FmReceiveStatus s = fm->status();
    carrierGate.update(s.carrier);
    // A carrier, debounced, keeps battery save off, so a tone detector has
    // time to lock before the speaker opens. A single poll does not: the
    // first one after the receiver wakes can read a carrier that is not
    // there.
    if (carrierGate.heard()) {
        power::activity();
    }
    toneGate.update(s.toneHeard);
    const bool open = gateOpen();
    if (open != speakerOpen) {
        const char *what = toneSquelch() ? (open ? "squelch open, tone heard" : "squelch closed, tone gone")
                                         : (open ? "squelch open" : "squelch closed");
        fm->logStatus(what);
        setSpeaker(open);
    }
}

void startTransmit()
{
    if (transmitting) {
        return;
    }
    if (fmChannel.digital) {
        log::info("dmr") << "PTT: DMR transmit is not built yet";
        return;
    }
    if (!fm->receiving() || system::calibration() == nullptr) {
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
    withBaseband([](auto &b) {
        b.setMicrophone(transmitAudio != FmTransmitAudio::Route::Direct);
        b.setWorkMode(services::BasebandWorkMode::Transmit);
    });
    channel.transmitTone = transmitTone;
    const char *problem = fm->transmit(channel, powerLevel);
    if (problem != nullptr) {
        fm->endTransmit();
        withBaseband([](auto &b) { b.setMicrophone(false); });
        log::error("fm") << "transmit refused at " << mhz(channel.transmitHz).view() << " MHz: " << problem;
        receive();
        return;
    }
    if (transmitAudio == FmTransmitAudio::Route::Baseband) {
        fm->setMicrophoneMuted(true);
    }
    transmitting = true;
    power::hold(power::AwakeReason::Transmit);
    ui::post(ui::Transmitting{true});
    transmitStarted = os::nowMs();
    log::info("fm") << "transmitting " << mhz(channel.transmitHz).view() << " MHz, power " << powerName(powerLevel)
                    << ", " << toneText(transmitTone).view() << ", timeout " << timeoutS << " s, audio "
                    << routeName(transmitAudio);
}

void stopTransmit(const char *why)
{
    if (!transmitting) {
        return;
    }
    fm->endTransmit();
    withBaseband([](auto &b) { b.setMicrophone(false); });
    transmitting = false;
    power::release(power::AwakeReason::Transmit);
    ui::post(ui::Transmitting{false});
    log::info("fm") << "transmit off (" << why << ") after " << (os::nowMs() - transmitStarted) / 100 / 10 << "."
                    << (os::nowMs() - transmitStarted) / 100 % 10 << " s";
    receive();
}

void bringUp()
{
    withBaseband([](auto &b) {
        log::info("baseband") << "starting";
        const os::Ticks started = os::nowMs();
        b.start();
        log::info("baseband") << "start took " << (os::nowMs() - started) << " ms";
        report();
        if (speakerVolume >= 0) {
            b.setSpeakerVolume(speakerVolume);
        }
        // A restart forgets the analog or digital set-up.
        if (fm->receiving() && system::calibration() != nullptr) {
            if (fmChannel.digital) {
                b.setDigital(dmrSettings, dmrRepeater);
            } else {
                basebandAnalogReceive();
            }
        }
    });
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
    // Battery save sleeps only an FM receiver; a DMR receiver would lose
    // the signal's timing.
    const bool receiverOn = !fmChannel.digital && !transmitting && (fm->receiving() || fm->receiverAsleep());
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
        if (fm->receiverAsleep()) {
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
                       withBaseband([&t](auto &b) {
                           const bool ok = b.playTone(t.hz, t.ms, ChipTonePercent);
                           log::info("baseband") << "tone " << t.hz << " Hz, " << t.ms << " ms: " << (ok ? "accepted" : "refused");
                       });
                   },
                   [](const SpeakerVolume &v) {
                       speakerVolume = v.step;
                       withBaseband([&v](auto &b) {
                           const bool ok = b.setSpeakerVolume(v.step);
                           log::info("baseband") << "speaker volume " << v.step << ": " << (ok ? "set" : "refused");
                       });
                   },
                   [](const FmReceive &r) {
                       const services::Tone receiveTone = fmChannel.receiveTone;
                       fmChannel = services::FmChannel{r.receiveHz, r.receiveHz, r.wide, {}, {}, false};
                       dmrSettings = {};
                       fmChannel.receiveTone = receiveTone;
                       receive();
                   },
                   [](const FmIdle &) {
                       fm->idle();
                       setSpeaker(false);
                       withBaseband([](auto &b) { b.setWorkMode(services::BasebandWorkMode::Idle); });
                       log::info("fm") << "idle";
                   },
                   [](const FmSquelch &s) {
                       squelchLevel = s.level;
                       if (fm->receiving()) {
                           receive();
                       } else {
                           log::info("fm") << "squelch " << squelchLevel;
                       }
                   },
                   [](const FmStatus &) { fmStatus(); },
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
                   [](const FmTone &t) {
                       if (t.transmit) {
                           transmitTone = t.tone;
                           log::info("fm") << "transmit " << toneText(transmitTone).view();
                           return;
                       }
                       fmChannel.receiveTone = t.tone;
                       log::info("fm") << "receive " << toneText(t.tone).view();
                       if (fm->receiving()) {
                           receive();
                       }
                   },
                   [](const FmPower &p) {
                       powerLevel = p.level;
                       log::info("fm") << "transmit power " << powerName(powerLevel);
                   },
                   [](const FmTransmitAudio &a) {
                       transmitAudio = a.route;
                       log::info("fm") << "transmit audio: " << routeName(a.route);
                   },
                   [](const FmTimeout &t) {
                       timeoutS = t.seconds;
                       log::info("fm") << "transmit timeout " << timeoutS << " s";
                   },
                   [](const DmrReceive &r) {
                       fmChannel = services::FmChannel{r.receiveHz, r.receiveHz, false, {}, {}, true};
                       dmrSettings = {};
                       dmrSettings.colourCode = r.colourCode;
                       dmrSettings.slot = r.slot == 2 ? 2 : 1;
                       dmrRepeater = r.slot != 0;
                       transmitHz = r.receiveHz;
                       dmrReceive();
                   },
                   [](const UseChannel &u) {
                       const services::Channel &c = u.channel;
                       const bool toneSquelch = c.squelchMode == services::SquelchMode::Tone && c.analog();
                       fmChannel = services::FmChannel{c.receiveHz, c.transmitHz, c.wide && c.analog(),
                           toneSquelch ? c.receiveTone : services::Tone{}, c.analog() ? c.transmitTone : services::Tone{},
                           c.digital()};
                       dmrSettings = c.dmr;
                       // A digital channel works through a repeater when it
                       // receives and transmits on different frequencies.
                       dmrRepeater = c.receiveHz != c.transmitHz;
                       transmitHz = c.transmitHz;
                       transmitTone = c.transmitTone;
                       powerLevel = c.power;
                       receiveOnly = c.receiveOnly;
                       squelchLevel = u.squelch;
                       receive();
                   },
                   [](const BatterySave &s) {
                       saver.setMode(s.mode);
                       log::info("power") << "battery save " << saveModeText(saver.mode());
                   },
                   [](const BatterySaveStatus &) { saveStatus(); },
                   [](const Microphone &m) {
                       fm->setMicrophoneMuted(!m.on);
                       log::info("fm") << "microphone " << (m.on ? "on" : "muted");
                   },
                   [](const ChipCommand &c) {
                       bool known = false;
                       if (c.chip == ChipCommand::Chip::Fm) {
                           known = fm->debugCommand(c.text.view());
                       } else {
                           withBaseband([&](auto &b) { known = b.debugCommand(c.text.view()); });
                       }
                       if (!known) {
                           log::info("console") << "unknown command";
                       }
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
        const bool busy = fm->receiving() || transmitting;
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
        if (fm->receiving() && os::nowMs() - lastPoll >= FmPollMs) {
            lastPoll = os::nowMs();
            if (fmChannel.digital) {
                pollDmr();
            } else {
                pollFm();
            }
        }
    }
}

} // namespace

void start(board::Fm &f, board::Baseband *b)
{
    fm = &f;
    baseband = b;
    radioTask.start("radio", 7, radioMain, nullptr);
}

bool post(const Message &message)
{
    return fm != nullptr && inbox.send(message);
}

} // namespace dmm::app::radio
