// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "radio.h"

#include "audio.h"
#include "radio_internal.h"
#include "roaming.h"
#include "system.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/battery_saver.h"
#include "dmm/services/log.h"
#include "dmm/services/power.h"
#include "dmm/services/roaming.h"
#include "dmm/services/squelch_gate.h"

#include <algorithm>
#include <atomic>
#include <span>
#include <string_view>

namespace dmm::app::radio {

namespace internal {

namespace log = services::log;
namespace power = services::power;

board::Fm *fm = nullptr;
board::Baseband *baseband = nullptr;

// The board's baseband, if it has one.
template <class F>
void withBaseband(F &&f)
{
    app::withBaseband(baseband, f);
}
// Room for the deepest path, a channel change down to a baseband packet,
// with an interrupt on top and a margin.
os::Task<768> radioTask;
os::Queue<Message, 8> inbox;
// What the radio does on the air, kept for other tasks.
std::atomic<Activity> airActivity{Activity::Idle};
// A carrier on the channel's own frequency, for the green LED.
std::atomic<bool> carrierShown{false};
bool onAir = false;
// The chips are up: set after the start or a restart of the baseband.
std::atomic<bool> chipsReady{false};
// The last speaker volume asked for, applied again after every start.
int speakerVolume = -1;

// Half of full scale: loud enough to hear, short of clipping.
constexpr int ChipTonePercent = 50;

// FM receive: the channel, the squelch level and the speaker gate. A
// channel with a receive tone opens the speaker on the tone alone, else
// on the carrier; squelch level 0 forces only the carrier open.
constexpr os::Ticks FmPollMs = 10;
// On a DMR channel the receiver's carrier is read this often.
constexpr os::Ticks DmrCarrierPollMs = 50;
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
// Who the call heard is from and to.
std::uint32_t callSource = 0;
std::uint32_t callDestination = 0;
bool timingFound = false;
// DMR transmit for the session: a target and own ID override the
// channel's when set.
services::dmr::Target targetOverride;
std::uint32_t ownIdOverride = 0;
services::dmr::FullLc callLc;
// The receive group list's talkgroups, set by the UI task.
os::Mutex groupsLock;
std::uint32_t receiveGroupIds[services::CodeplugSource::MaxReceiveGroups];
std::size_t receiveGroupCount = 0;
// The call being left muted because it is not for this radio.
bool callIgnored = false;
constexpr std::uint32_t AllCallId = 0xFFFFFF;
// The repeater's carrier is heard now.
bool carrierHeard = false;
bool timingSeen = false;
bool idleLogged = false;
// The colour code of the last burst heard, for roaming's checks.
std::uint8_t burstColourCode = NoColourCode;
services::BatterySaver saver;

// A console PTT that releases itself.
bool pttTimed = false;
os::Ticks pttReleaseAt = 0;
// Messages that came during a search, handled after it.
Message deferred[4];
std::size_t deferredCount = 0;

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

// How long the end of a DMR transmission may take to go out.
constexpr os::Ticks DmrEndMs = 1000;
// Channel access through a repeater, as ETSI TS 102 361-1 has it: with a
// repeater already on the air (a beacon, the end of a call), wait for its
// timing up to T_TxCCSlot; else wake it with one block, then wait for its
// timing up to T_SyncWu, at most N_Wakeup times (RepeaterWakeTries).
constexpr os::Ticks FindSyncMs = 720;
constexpr os::Ticks RepeaterWakeMs = 360;
// Reports this soon after a retune may still come from the frequency left.
constexpr os::Ticks StaleMs = 20;

// Transmit only inside the 2 m and 70 cm amateur bands.
struct Band
{
    std::uint32_t lowHz;
    std::uint32_t highHz;
};
constexpr Band TransmitBands[] = {{144'000'000, 146'000'000}, {430'000'000, 440'000'000}};

std::uint32_t ownDmrId()
{
    return ownIdOverride != 0 ? ownIdOverride : dmrSettings.ownId;
}

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

// What the radio does on the air now. FM is heard while the squelch gate
// holds the speaker open; DMR also whenever its slot timing is found, a
// call or not: decoded DMR, not a bare carrier, which may be another
// signal.
void updateActivity()
{
    airActivity.store(onAir                                     ? Activity::Transmit
            : fmChannel.digital && (speakerOpen || timingFound) ? Activity::DmrReceive
            : speakerOpen                                       ? Activity::FmReceive
                                                                : Activity::Idle);
}

// The receiver's carrier decision, on the channel's own frequency only:
// a roaming check elsewhere shows nothing.
void showCarrier(bool heard)
{
    carrierShown.store(roam::home() && heard);
}

// The screen's and LEDs' transmit indication.
void showTransmitting(bool on)
{
    onAir = on;
    updateActivity();
    ui::post(ui::Transmitting{on});
}

void setSpeaker(bool open)
{
    if (open == speakerOpen) {
        return;
    }
    speakerOpen = open;
    updateActivity();
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

// MHz with three decimals, four when the frequency needs them, for the
// log.
// The frequency a DMR call goes out on.
std::uint32_t callChannelHz()
{
    return transmitHz != 0 ? transmitHz : fmChannel.receiveHz;
}

lib::StaticString<12> mhz(std::uint32_t hz)
{
    const std::uint32_t tenthKhz = hz / 100;
    const std::uint32_t frac = tenthKhz % 10000;
    lib::StaticString<12> text;
    text << tenthKhz / 10000 << '.' << static_cast<char>('0' + frac / 1000) << static_cast<char>('0' + frac / 100 % 10)
         << static_cast<char>('0' + frac / 10 % 10);
    if (frac % 10 != 0) {
        text << static_cast<char>('0' + frac % 10);
    }
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

// Whether a call is for this radio: to the channel's or chosen
// talkgroup, one in the receive group list, its own ID or everyone. With
// no talkgroup or ID set at all (console listening) every call is heard.
[[gnu::noinline]] bool forUs(const services::dmr::FullLc &lc)
{
    using services::dmr::CallType;
    const std::uint32_t ownId = ownDmrId();
    if (lc.destination == AllCallId) {
        return true;
    }
    if (lc.privateCall()) {
        return ownId == 0 || lc.destination == ownId;
    }
    if (!lc.groupCall()) {
        return true;
    }
    const auto talkgroup = [&lc](const services::dmr::Target &t) {
        return t.id != 0 && t.type == CallType::Group && t.id == lc.destination;
    };
    if (talkgroup(dmrSettings.contact) || talkgroup(targetOverride)) {
        return true;
    }
    groupsLock.lock();
    const std::size_t groups = receiveGroupCount;
    const bool listed = std::find(receiveGroupIds, receiveGroupIds + groups, lc.destination) != receiveGroupIds + groups;
    groupsLock.unlock();
    return listed || (dmrSettings.contact.id == 0 && targetOverride.id == 0 && groups == 0);
}

void endCall(const char *why)
{
    // The gap between overs counts from the call's end, also when it ends
    // without a terminator.
    if (inCall || callIgnored) {
        roam::callHeard();
    }
    callIgnored = false;
    if (!inCall) {
        return;
    }
    inCall = false;
    ui::post(ui::DmrCall{false, false, 0, 0});
    logCallEnd(why, os::nowMs() - callStarted);
    setSpeaker(false);
}

// The DMR channel's transmit frequency: its own, else the receive one.
std::uint32_t dmrTransmitHz()
{
    return fmChannel.transmitHz != 0 ? fmChannel.transmitHz : fmChannel.receiveHz;
}

// Tunes to the DMR channel and lets the baseband listen.
void dmrReceive()
{
    endCall("left");
    setSpeaker(false);
    timingFound = false;
    updateActivity();
    timingSeen = false;
    if (system::calibration() == nullptr) {
        logDmr("receive refused: no calibration loaded");
        return;
    }
    const char *problem = fm->receive(fmChannel, 1);
    saver.reset();
    carrierGate.reset();
    if (problem != nullptr) {
        logReceiveRefused(problem);
        return;
    }
    bool ok = !HasBaseband;
    withBaseband([&ok](auto &b) { ok = b.setDigital(dmrSettings, fmChannel.receiveHz, dmrTransmitHz(), dmrRepeater); });
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
        roam::slotTiming();
        log::debug("dmr") << "timing: " << (e.baseStation ? "repeater" : "direct") << (e.verified ? ", verified" : "")
                          << (e.inverted ? ", inverted" : "") << (e.inboundBusy ? ", busy" : "") << ", next slot "
                          << e.nextSlot;
        if (!timingFound && e.verified) {
            timingFound = true;
            updateActivity();
            const int dbm = fm->status().rssiDbm;
            roam::level(dbm);
            log::info("dmr") << (e.baseStation ? "repeater" : "direct") << " timing found, " << dbm << " dBm"
                             << (e.inboundBusy ? ", inbound busy" : "");
        } else if (!timingFound && !timingSeen) {
            timingSeen = true;
            log::info("dmr") << (e.baseStation ? "repeater" : "direct") << " timing seen, not yet verified";
        }
        break;
    case Kind::Burst: {
        burstColourCode = e.colourCode;
        roam::burst(e.colourCode, e.voice || e.type != DataType::Idle);
        const bool lcBurst = e.voice || e.type == DataType::VoiceLcHeader || e.type == DataType::TerminatorWithLc;
        const auto lc = lcBurst ? services::dmr::FullLc::decode(e.payload()) : std::nullopt;
        // Idle bursts repeat the same fill; the first of each transmission
        // is enough for the log.
        if (e.type != DataType::Idle || !idleLogged) {
            idleLogged = idleLogged || e.type == DataType::Idle;
            lib::StaticString<40> bytes;
            for (std::size_t i = 0; i < e.length; ++i) {
                bytes << lib::hex(e.data[i], 2);
            }
            log::debug("dmr") << "burst " << lib::hex(e.raw, 2) << ", colour code " << e.colourCode << ", "
                              << fm->status().rssiDbm << " dBm: " << bytes.view();
        }
        // Only the channel's colour code carries calls for us; a weak
        // burst can also come with a wrong one.
        if (!lc || e.colourCode != dmrSettings.colourCode) {
            break;
        }
        if (!e.voice && e.type == DataType::TerminatorWithLc) {
            endCall("ended");
            break;
        }
        // Other link control (talker alias, position) carries no caller.
        if (!lc->groupCall() && !lc->privateCall()) {
            break;
        }
        // Overs can follow each other without a terminator heard between
        // them: a new caller or target is a new call.
        if (inCall && (lc->source != callSource || lc->destination != callDestination)) {
            endCall("ended");
        }
        if (!inCall && !forUs(*lc)) {
            if (!callIgnored) {
                callIgnored = true;
                log::info("dmr") << "call from " << lc->source << " to " << lc->destination << ": not for us";
            }
            break;
        }
        if (!inCall) {
            inCall = true;
            callStarted = os::nowMs();
            callSource = lc->source;
            callDestination = lc->destination;
            log::info("dmr") << (lc->groupCall() ? "group call" : lc->privateCall() ? "private call"
                                                                                    : "call")
                             << " from "
                             << lc->source << " to " << lc->destination << ", colour code " << e.colourCode
                             << (lc->emergency() ? ", emergency" : "");
            ui::post(ui::DmrCall{true, lc->groupCall(), lc->source, lc->destination});
            setSpeaker(true);
        }
        break;
    }
    case Kind::SyncLost: {
        // Through a repeater a call is on the channel's slot. Direct, the
        // call's slot is not known: the call is left to its terminator and
        // the carrier.
        if (inCall && !dmrRepeater) {
            break;
        }
        const bool ownSlot = !dmrRepeater || e.slot == dmrSettings.slot;
        if (ownSlot) {
            timingFound = false;
            updateActivity();
            timingSeen = false;
        }
        if (inCall && ownSlot) {
            endCall("lost");
            break;
        }
        // A slot without a call: its timing is searched for again.
        withBaseband([&e](auto &b) {
            if (!b.searchSlot(e.slot)) {
                log::debug("dmr") << "slot " << e.slot << " search not taken";
            }
        });
        break;
    }
    case Kind::CarrierFound:
        idleLogged = false;
        carrierHeard = true;
        [[fallthrough]];
    case Kind::CarrierLost:
        log::debug("dmr") << (e.kind == Kind::CarrierFound ? "carrier found" : "carrier lost");
        if (e.kind == Kind::CarrierLost) {
            carrierHeard = false;
            // A repeater that went quiet must be woken again before a call.
            timingFound = false;
            updateActivity();
            timingSeen = false;
            endCall("lost");
        }
        break;
    case Kind::AllSent:
        break;
    case Kind::Other: {
        lib::StaticString<64> bytes;
        for (std::size_t i = 0; i < e.length; ++i) {
            bytes << lib::hex(e.data[i], 2) << ' ';
        }
        log::debug("dmr") << "report " << lib::hex(e.raw, 2) << ": " << bytes.view();
        break;
    }
    }
}

// One pass over what the baseband heard.
// The level summary: every reading of a minute on a DMR channel's own
// frequency, in 1 dB steps, then the lowest tenth, the middle and the
// highest logged. Shows the noise and a signal such as a beacon apart.
std::atomic<bool> levelLog{false};
constexpr int LevelLowestDbm = -140;
constexpr std::size_t LevelSteps = 100;
constexpr os::Ticks LevelPeriodMs = 60'000;

void noteLevel(int dbm)
{
    static std::uint16_t counts[LevelSteps] = {};
    static unsigned readings = 0;
    static os::Ticks started = 0;
    if (!levelLog.load(std::memory_order_relaxed)) {
        readings = 0;
        return;
    }
    if (readings == 0) {
        std::fill(std::begin(counts), std::end(counts), 0);
        started = os::nowMs();
    }
    const int step = std::clamp(dbm - LevelLowestDbm, 0, static_cast<int>(LevelSteps) - 1);
    ++counts[step];
    ++readings;
    if (os::nowMs() - started < LevelPeriodMs) {
        return;
    }
    // The level at which @p share of the readings lie at or below.
    auto at = [&](unsigned share) {
        unsigned seen = 0;
        for (std::size_t i = 0; i < LevelSteps; ++i) {
            seen += counts[i];
            if (seen * 100 >= readings * share) {
                return static_cast<int>(i) + LevelLowestDbm;
            }
        }
        return LevelLowestDbm + static_cast<int>(LevelSteps) - 1;
    };
    log::info("dmr") << "level over " << (os::nowMs() - started) / 1000 << " s: low " << at(10) << ", middle " << at(50)
                     << ", high " << at(100) << " dBm, " << readings << " readings";
    readings = 0;
}

void pollDmr()
{
    // The carrier for the green LED: read less often than on FM.
    static os::Ticks carrierPolled = 0;
    if (os::nowMs() - carrierPolled >= DmrCarrierPollMs) {
        carrierPolled = os::nowMs();
        const services::FmReceiveStatus s = fm->status();
        carrierGate.update(s.carrier);
        showCarrier(carrierGate.heard());
        noteLevel(s.rssiDbm);
    }
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
    showCarrier(carrierGate.heard());
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

// Waits up to @p ms for the baseband event @p kind, handling every other
// event as usual. True if it came.
bool awaitDmrEvent(services::dmr::Event::Kind kind, os::Ticks ms)
{
    bool seen = false;
    const os::Ticks start = os::nowMs();
    while (!seen && os::nowMs() - start < ms) {
        withBaseband([&](auto &b) {
            services::dmr::Event e;
            while (!seen && b.nextEvent(e)) {
                onDmrEvent(e);
                // Slot timing counts once it is verified.
                seen = kind == services::dmr::Event::Kind::SlotFound ? timingFound : e.kind == kind;
            }
        });
        if (!seen) {
            os::sleepMs(10);
        }
    }
    return seen;
}

bool carrierOnAir()
{
    if (!carrierHeard && fm->status().carrier) {
        carrierHeard = true;
        log::debug("dmr") << "carrier on the air";
    }
    return carrierHeard;
}

bool awaitCarrier(os::Ticks ms)
{
    const os::Ticks start = os::nowMs();
    while (!carrierOnAir() && os::nowMs() - start < ms) {
        awaitDmrEvent(services::dmr::Event::Kind::CarrierFound, 10);
    }
    return carrierHeard;
}

void dropStaleReports(os::Ticks ms)
{
    const os::Ticks start = os::nowMs();
    while (os::nowMs() - start < ms) {
        withBaseband([](auto &b) {
            services::dmr::Event e;
            while (b.nextEvent(e)) {
            }
        });
        os::sleepMs(5);
    }
}

// Messages that come during a search wait for its end; PTT, a channel
// change or another search end it early, as does a full wait list.
bool searchInterrupted()
{
    Message m;
    while (deferredCount < std::size(deferred) && inbox.receive(m, 0)) {
        deferred[deferredCount++] = m;
        if (std::holds_alternative<Ptt>(m) || std::holds_alternative<PttFor>(m) || std::holds_alternative<UseChannel>(m) ||
            std::holds_alternative<RoamNow>(m) || std::holds_alternative<FmReceive>(m) ||
            std::holds_alternative<DmrReceive>(m) || std::holds_alternative<FmIdle>(m) ||
            std::holds_alternative<Restart>(m)) {
            return true;
        }
    }
    return deferredCount == std::size(deferred);
}

bool canRoam()
{
    return !transmitting && !inCall && deferredCount == 0 && fm->receiving();
}

// PTT went up while a search ran, or the console's timed PTT ran out.
bool pttReleasedMeanwhile()
{
    if (pttTimed && static_cast<std::int32_t>(os::nowMs() - pttReleaseAt) >= 0) {
        return true;
    }
    for (std::size_t i = 0; i < deferredCount; ++i) {
        if (const Ptt *p = std::get_if<Ptt>(&deferred[i]); p != nullptr && !p->down) {
            return true;
        }
    }
    return false;
}

[[gnu::noinline]] void logWakeTry(int attempt, bool sent, bool slow, bool answered)
{
    const char *what = "no answer";
    if (!sent) {
        what = "not sent";
    } else if (answered) {
        what = "answered";
    } else if (slow) {
        what = "carrier, no timing";
    }
    log::info("dmr") << "repeater wake-up " << attempt << ": " << what;
}

// A refused PTT: the reason to the log, a short one on the screen.
[[gnu::noinline]] void refused(const char *tag, const char *why, const char *shown)
{
    log::info(tag) << "PTT: " << why;
    ui::post(ui::Notice{shown});
}

void logDmrRefused(const char *why, const char *shown)
{
    refused("dmr", why, shown);
}

[[gnu::noinline]] void logDmrTransmitting()
{
    log::info("dmr") << "transmitting " << mhz(callChannelHz()).view() << " MHz, slot " << dmrSettings.slot << ", "
                     << (callLc.groupCall() ? "TG " : "private ") << callLc.destination << " from " << callLc.source
                     << ", power " << powerName(powerLevel);
}

enum class CallStart : std::uint8_t
{
    Started,
    Refused,
    NoRepeater, // the repeater did not answer its wake-up
};

CallStart startDigitalTransmit(services::FmChannel channel)
{
    if (!HasBaseband) {
        logDmrRefused("no baseband", "No DMR");
        return CallStart::Refused;
    }
    if (!transmitAllowed(channel.transmitHz)) {
        logDmrRefused("outside the 2 m and 70 cm bands", "TX not allowed");
        return CallStart::Refused;
    }
    if (inCall) {
        logDmrRefused("the channel is busy", "Channel busy");
        return CallStart::Refused;
    }
    if (dmrRepeater && channel.transmitHz == fmChannel.receiveHz) {
        logDmrRefused("no repeater input frequency (fm tx <kHz>)", "No TX frequency");
        return CallStart::Refused;
    }
    const services::dmr::Target target = targetOverride.id != 0 ? targetOverride : dmrSettings.contact;
    const std::uint32_t ownId = ownDmrId();
    if (target.id == 0 && target.type != services::dmr::CallType::All) {
        logDmrRefused("no contact to call (dmr tg <id>)", "No contact");
        return CallStart::Refused;
    }
    if (ownId == 0) {
        logDmrRefused("no own DMR ID (dmr id <id>)", "No own DMR ID");
        return CallStart::Refused;
    }
    callLc = services::dmr::FullLc::voiceCall(target, ownId);
    setSpeaker(false);
    // A repeater on the air without known timing yet: its timing comes
    // soon, without a wake-up.
    if (dmrRepeater && !timingFound && carrierHeard) {
        const bool found = awaitDmrEvent(services::dmr::Event::Kind::SlotFound, FindSyncMs);
        log::info("dmr") << "repeater on the air: timing " << (found ? "found" : "not found");
    }
    // An idle repeater is woken first, then its timing awaited.
    if (dmrRepeater && !timingFound) {
        const char *problem = nullptr;
        const Wake w = wakeRepeater(RepeaterWakeTries, problem);
        if (w == Wake::Refused) {
            logDmrRefused(problem, "TX refused");
            receive();
            return CallStart::Refused;
        }
        if (w == Wake::NoAnswer) {
            // On a roaming channel a silent repeater starts a search instead.
            if (roam::channel()) {
                log::info("dmr") << "PTT: the repeater did not answer";
            } else {
                logDmrRefused("the repeater did not answer", "No repeater");
            }
            receive();
            return CallStart::NoRepeater;
        }
    }
    const char *problem = fm->transmit(channel, powerLevel);
    if (problem != nullptr) {
        fm->endTransmit();
        logDmrRefused(problem, "TX refused");
        receive();
        return CallStart::Refused;
    }
    // On the air from here.
    showTransmitting(true);
    bool ok = false;
    // timingFound is cleared as soon as the repeater's carrier goes, so it
    // is never stale here.
    withBaseband([&](auto &b) {
        ok = b.startDigitalCall(dmrSettings, callLc, channel.transmitHz, dmrRepeater && timingFound, powerLevel);
    });
    if (!ok) {
        fm->endTransmit();
        logDmrRefused("the baseband refused", "TX refused");
        showTransmitting(false);
        receive();
        return CallStart::Refused;
    }
    transmitting = true;
    power::hold(power::AwakeReason::Transmit);
    transmitStarted = os::nowMs();
    logDmrTransmitting();
    return CallStart::Started;
}

// The current channel as a call goes out on it.
services::FmChannel callChannel()
{
    services::FmChannel channel = fmChannel;
    channel.transmitHz = transmitHz != 0 ? transmitHz : fmChannel.receiveHz;
    return channel;
}

// ---- Radio operations for roaming (radio_internal.h)

void useRepeater(std::uint32_t receiveHz, std::uint32_t transmitHzIn, std::uint8_t colourCode)
{
    fmChannel.receiveHz = receiveHz;
    fmChannel.transmitHz = transmitHzIn;
    dmrSettings.colourCode = colourCode;
    transmitHz = fmChannel.transmitHz;
    dmrRepeater = fmChannel.receiveHz != fmChannel.transmitHz;
}

std::uint32_t tunedHz()
{
    return fmChannel.receiveHz;
}

std::uint8_t tunedColourCode()
{
    return dmrSettings.colourCode;
}

void leaveCall()
{
    endCall("left");
    setSpeaker(false);
}

Heard listenHere(os::Ticks carrierMs, os::Ticks timingMs, os::Ticks burstMs)
{
    Heard h;
    carrierHeard = false;
    dmrReceive();
    dropStaleReports(StaleMs);
    h.cut = searchInterrupted();
    if (!h.cut && awaitCarrier(carrierMs) && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, timingMs)) {
        burstColourCode = NoColourCode;
        awaitDmrEvent(services::dmr::Event::Kind::Burst, burstMs);
        h.colourCode = burstColourCode;
    }
    h.rssiDbm = fm->status().rssiDbm;
    return h;
}

bool onAirHere(os::Ticks carrierMs)
{
    carrierHeard = false;
    dmrReceive();
    awaitDmrEvent(services::dmr::Event::Kind::CarrierFound, carrierMs);
    return carrierHeard && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, FindSyncMs);
}

// Each try keys the transmitter for one wake-up block, the carrier on
// through it as there is no slot timing yet, then listens for the
// repeater's timing. A carrier without timing yet is a repeater slower to
// start: it gets as long as one already on the air.
Wake wakeRepeater(int tries, const char *&problem)
{
    services::FmChannel wake = callChannel();
    wake.continuous = true;
    const std::uint32_t id = ownDmrId();
    bool answered = false;
    problem = nullptr;
    withBaseband([&](auto &b) {
        for (int t = 0; t < tries && !answered; ++t) {
            problem = fm->transmit(wake, powerLevel);
            if (problem != nullptr) {
                fm->endTransmit();
                return;
            }
            showTransmitting(true);
            const bool sent = b.wakeRepeater(dmrSettings, id) && awaitDmrEvent(services::dmr::Event::Kind::AllSent, DmrEndMs);
            fm->endTransmit();
            showTransmitting(false);
            const bool listening = sent && b.setDigital(dmrSettings, fmChannel.receiveHz, dmrTransmitHz(), dmrRepeater) &&
                fm->receive(fmChannel, 1) == nullptr;
            answered = listening && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, RepeaterWakeMs);
            const bool slow = listening && !answered && carrierHeard;
            if (slow) {
                answered = awaitDmrEvent(services::dmr::Event::Kind::SlotFound, FindSyncMs);
            }
            logWakeTry(t + 1, sent, slow, answered);
        }
    });
    if (answered) {
        return Wake::Answered;
    }
    return problem != nullptr ? Wake::Refused : Wake::NoAnswer;
}

int readLevel()
{
    constexpr int Reads = 8;
    int sum = 0;
    for (int r = 0; r < Reads; ++r) {
        sum += fm->status().rssiDbm;
        os::sleepMs(10);
    }
    return sum / Reads;
}

void backHere()
{
    carrierHeard = false;
    receive();
    dropStaleReports(StaleMs);
    carrierOnAir();
}

// A call on a roaming channel goes through the repeater in use. With none,
// or when it does not answer, automatic roaming searches for one and the
// call gets one more try; without it the radio stays and says so.
void startRoamingCall()
{
    const bool known = roam::inUse() >= 0;
    if (known && startDigitalTransmit(callChannel()) != CallStart::NoRepeater) {
        return;
    }
    if (known) {
        roam::unanswered();
    }
    if (!roam::automaticOn()) {
        logDmrRefused(known ? "the repeater did not answer (automatic roaming off)" : "no repeater in use (automatic roaming off)",
            known ? "No answer" : "No repeater");
        return;
    }
    if (!roam::search(roam::SearchCause::Ptt, known) || pttReleasedMeanwhile()) {
        return;
    }
    if (startDigitalTransmit(callChannel()) == CallStart::NoRepeater) {
        roam::unanswered();
        ui::post(ui::Notice{"No repeater answered"});
    }
}

void startTransmit()
{
    if (transmitting) {
        return;
    }
    if (!fm->receiving() || system::calibration() == nullptr) {
        refused("fm", "no FM channel set (fm rx first)", "No channel");
        return;
    }
    services::FmChannel channel = fmChannel;
    channel.transmitHz = transmitHz != 0 ? transmitHz : fmChannel.receiveHz;
    if (fmChannel.digital) {
        if (receiveOnly) {
            logDmrRefused("this channel is receive only", "Receive only");
        } else if (roam::channel()) {
            startRoamingCall();
        } else {
            startDigitalTransmit(channel);
        }
        return;
    }
    if (receiveOnly) {
        refused("fm", "this channel is receive only", "Receive only");
        return;
    }
    if (!transmitAllowed(channel.transmitHz)) {
        log::warning("fm") << "PTT: " << mhz(channel.transmitHz).view() << " MHz is outside the 2 m and 70 cm bands";
        ui::post(ui::Notice{"TX not allowed"});
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
        ui::post(ui::Notice{"TX refused"});
        receive();
        return;
    }
    if (transmitAudio == FmTransmitAudio::Route::Baseband) {
        fm->setMicrophoneMuted(true);
    }
    transmitting = true;
    power::hold(power::AwakeReason::Transmit);
    showTransmitting(true);
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
    if (fmChannel.digital) {
        // The terminator goes out before the RF path drops.
        bool sent = false;
        withBaseband([&sent](auto &b) { sent = b.endDigitalCall(dmrSettings, callLc); });
        if (sent && !awaitDmrEvent(services::dmr::Event::Kind::AllSent, DmrEndMs)) {
            log::warning("dmr") << "the terminator did not finish in time";
        }
    }
    fm->endTransmit();
    withBaseband([](auto &b) { b.setMicrophone(false); });
    transmitting = false;
    power::release(power::AwakeReason::Transmit);
    showTransmitting(false);
    log::info("fm") << "transmit off (" << why << ") after " << (os::nowMs() - transmitStarted) / 100 / 10 << "."
                    << (os::nowMs() - transmitStarted) / 100 % 10 << " s";
    receive();
}

void bringUp()
{
    chipsReady.store(false);
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
                b.setDigital(dmrSettings, fmChannel.receiveHz, dmrTransmitHz(), dmrRepeater);
            } else {
                basebandAnalogReceive();
            }
        }
    });
    chipsReady.store(true);
    log::info("radio") << "ready";
}

// The baseband can stop taking commands and stay so until it restarts;
// after this many in a row it is started again.
constexpr int RefusedBeforeRestart = 10;

void restartIfRefusing()
{
    int refused = 0;
    withBaseband([&refused](auto &b) { refused = b.info().refusedInRow; });
    if (refused < RefusedBeforeRestart) {
        return;
    }
    log::warning("baseband") << refused << " commands in a row not taken; restarting it";
    if (transmitting) {
        stopTransmit("baseband restart");
    }
    bringUp();
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
                       roam::leave();
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
                   [](const PttFor &p) {
                       if (!chipsReady.load()) {
                           log::info("radio") << "PTT: not ready yet";
                           return;
                       }
                       log::info("radio") << "console PTT for " << p.ms << " ms";
                       pttReleaseAt = os::nowMs() + p.ms;
                       pttTimed = true;
                       startTransmit();
                   },
                   [](const Ptt &p) {
                       if (p.down && !chipsReady.load()) {
                           log::info("radio") << "PTT: not ready yet";
                           return;
                       }
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
                       roam::leave();
                       dmrSettings.colourCode = r.colourCode;
                       dmrSettings.slot = r.slot == 2 ? 2 : 1;
                       dmrRepeater = r.slot != 0;
                       // Direct: transmit where it receives. Through a
                       // repeater the input frequency comes from fm tx.
                       transmitHz = r.slot == 0 ? r.receiveHz : 0;
                       dmrReceive();
                   },
                   [](const DmrTarget &t) {
                       targetOverride = t.target;
                       if (t.target.id == 0) {
                           log::info("dmr") << "calling the channel's contact";
                       } else {
                           log::info("dmr") << "calling " << (t.target.type == services::dmr::CallType::Group ? "TG " : "private ")
                                            << t.target.id;
                       }
                   },
                   [](const DmrOwnId &o) {
                       ownIdOverride = o.id;
                       log::info("dmr") << "own ID " << o.id << (o.id == 0 ? " (the channel's)" : "");
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
                       roam::useChannel(c);
                       receive();
                       roam::show(false);
                   },
                   [](const RoamNow &) { roam::searchNow(); },
                   [](const RoamingOn &r) { roam::setOn(r.on); },
                   [](const RoamAutomatic &r) { roam::setAutomatic(r.on); },
                   [](const RoamingSetup &r) { roam::setup(r.settings); },
                   [](const RoamStatus &) { roam::logStatus(); },
                   [](const BatterySave &s) {
                       saver.setMode(s.mode);
                       log::info("power") << "battery save " << saveModeText(saver.mode());
                   },
                   [](const BatterySaveStatus &) { saveStatus(); },
                   [](const VoiceEnhancement &v) {
                       withBaseband([&v](auto &b) {
                           const bool ok = b.setVoiceEnhancement(v.on);
                           log::info("baseband") << "voice enhancement " << (v.on ? "on" : "off") << (ok ? "" : ": refused");
                       });
                   },
                   [](const FmMicrophoneLevel &m) {
                       fm->setMicrophoneLevel(m.level);
                       log::info("fm") << "microphone level " << m.level + 1 << " of 5";
                   },
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
                   [](const GpsPower &g) {
                       fm->setGpsPower(g.on);
                       log::info("gps") << "receiver power " << (g.on ? "on" : "off");
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
        if (deferredCount > 0) {
            m = deferred[0];
            std::move(deferred + 1, deferred + deferredCount, deferred);
            --deferredCount;
            handle(m);
        } else if (inbox.receive(m, wait)) {
            handle(m);
        }
        if (pttTimed && static_cast<std::int32_t>(os::nowMs() - pttReleaseAt) >= 0) {
            pttTimed = false;
            stopTransmit("console PTT time");
        }
        if (inCall || callIgnored) {
            roam::callHeard();
        }
        roam::step();
        saveStep();
        restartIfRefusing();
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
            fm->keepLocked();
        }
    }
}

} // namespace internal

using namespace internal;

void start(board::Fm &f, board::Baseband *b)
{
    fm = &f;
    baseband = b;
    radioTask.start("radio", 7, radioMain, nullptr);
}

void setReceiveGroups(std::span<const std::uint32_t> ids)
{
    groupsLock.lock();
    receiveGroupCount = std::min(ids.size(), std::size(receiveGroupIds));
    std::copy_n(ids.begin(), receiveGroupCount, receiveGroupIds);
    groupsLock.unlock();
}

Activity activity()
{
    return airActivity.load();
}

bool carrier()
{
    return carrierShown.load();
}

void setLevelLog(bool on)
{
    levelLog.store(on, std::memory_order_relaxed);
}

bool ready()
{
    return chipsReady.load();
}

bool post(const Message &message)
{
    return fm != nullptr && inbox.send(message);
}

} // namespace dmm::app::radio
