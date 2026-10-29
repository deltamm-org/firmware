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
#include "dmm/services/roaming.h"
#include "dmm/services/squelch_gate.h"

#include <algorithm>
#include <atomic>
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
// What the radio does on the air, kept for other tasks.
std::atomic<Activity> airActivity{Activity::Idle};
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
// How long the end of a DMR transmission may take to go out.
constexpr os::Ticks DmrEndMs = 1000;
// Channel access through a repeater, as ETSI TS 102 361-1 has it: with a
// repeater already on the air (a beacon, the end of a call), wait for its
// timing up to T_TxCCSlot; else wake it with one block, then wait for its
// timing up to T_SyncWu, at most N_Wakeup times.
constexpr os::Ticks FindSyncMs = 720;
constexpr os::Ticks RepeaterWakeMs = 360;
constexpr int RepeaterWakeTries = 2;
// The repeater's carrier is heard now.
bool carrierHeard = false;
bool timingSeen = false;
bool idleLogged = false;
services::BatterySaver saver;

// Roaming: the roaming zone's repeaters, set by the UI task, and the one in
// use (-1: none yet). A roaming channel takes its frequencies and colour
// code from that repeater, unless the repeater leaves the colour code to
// the channel.
os::Mutex sitesLock;
services::RoamingSite sites[services::CodeplugSource::MaxRoamingSites];
std::size_t siteCount = 0;
bool sitesChanged = false;
bool roamingOn = true;
bool roamingChannel = false;
int site = -1;
// The roaming channel's own frequencies and colour code.
std::uint32_t channelReceiveHz = 0;
std::uint32_t channelTransmitHz = 0;
std::uint8_t channelColourCode = 1;
services::roaming::Watch roamWatch;
// The repeaters heard, under sitesLock: the UI task reads them.
services::roaming::HeardList heardList;
// The search order, and where the rest of the zone starts next time.
std::uint8_t searchList[services::CodeplugSource::MaxRoamingSites];
std::size_t restFrom = 0;
bool searchRunning = false;
// Tuned off the repeater in use: searching, or visiting another repeater.
bool away = false;
services::roaming::Visits visits;
services::roaming::RangeCheck rangeCheck;
// While the repeater in use is lost, a search wakes the zone at most this
// often.
constexpr std::uint32_t LostSearchMs = 30'000;
std::uint32_t lostSearchMs = 0;
constexpr services::roaming::Levels RoamLevels{};
// No move by itself within this time of the last one.
constexpr std::uint32_t MoveGuardMs = 15'000;
std::uint32_t lastMoveMs = 0;
// Visits wait until the repeater in use has been quiet, and the user idle,
// this long.
constexpr std::uint32_t BaseQuietMs = 10'000;
constexpr os::Ticks UserQuietMs = 5000;
// A missed beacon of the repeater in use counts this long after it was due.
constexpr std::uint32_t MissGraceMs = 1500;
// The repeater in use's current transmission: a short one of idle bursts
// only, not started by our own wake-up, is a beacon.
constexpr std::uint32_t MaxBeaconMs = 5000;
struct BaseTransmission
{
    bool on = false;
    bool idleOnly = true;
    bool measured = false;
    std::uint32_t startMs = 0;
    int dbm = 0;
};
BaseTransmission baseTx;
std::uint32_t baseHeardMs = 0;
std::uint32_t backOnBaseMs = 0;
std::uint32_t lastWakeMs = 0;
// For a visit: when the last carrier came, and the last burst's colour code.
std::uint32_t carrierSinceMs = 0;
std::uint8_t lastBurstColourCode = 0;
// A search stops at a repeater this strong.
constexpr int GoodDbm = -90;
// An automatic search moves only to a clearly stronger repeater.
constexpr int AutomaticMarginDb = 6;
// How long a probe listens for a repeater already on the air before it
// wakes one.
constexpr os::Ticks ProbeListenMs = 150;
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
void updateActivity()
{
    airActivity.store(onAir     ? Activity::Transmit
            : !speakerOpen      ? Activity::Idle
            : fmChannel.digital ? Activity::DmrReceive
                                : Activity::FmReceive);
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
    const std::uint32_t ownId = ownIdOverride != 0 ? ownIdOverride : dmrSettings.ownId;
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
    callIgnored = false;
    if (!inCall) {
        return;
    }
    inCall = false;
    ui::post(ui::DmrCall{false, false, 0, 0});
    logCallEnd(why, os::nowMs() - callStarted);
    setSpeaker(false);
}

// Tunes to the DMR channel and lets the baseband listen.
void dmrReceive()
{
    endCall("left");
    setSpeaker(false);
    timingFound = false;
    timingSeen = false;
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
    withBaseband([&ok](auto &b) { ok = b.setDigital(dmrSettings, fmChannel.receiveHz, dmrRepeater); });
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

// Repeater @p i was heard at @p dbm.
void noteHeard(int i, int dbm)
{
    sitesLock.lock();
    heardList.heard(static_cast<std::uint8_t>(i), dbm, os::nowMs());
    sitesLock.unlock();
}

[[gnu::noinline]] void baseTransmissionEnded()
{
    baseTx.on = false;
    const std::uint32_t length = os::nowMs() - baseTx.startMs;
    if (site < 0 || !baseTx.idleOnly || !baseTx.measured || length > MaxBeaconMs ||
        baseTx.startMs - lastWakeMs < 3000) {
        return;
    }
    sitesLock.lock();
    heardList.beacon(static_cast<std::uint8_t>(site), baseTx.dbm, baseTx.startMs);
    sitesLock.unlock();
    log::debug("roam") << "beacon of repeater " << site + 1 << ", " << baseTx.dbm << " dBm, " << length << " ms";
}

void onDmrEvent(const services::dmr::Event &e)
{
    using Kind = services::dmr::Event::Kind;
    using services::dmr::DataType;
    // DMR timing or a burst on the repeater's output counts as hearing it;
    // a carrier alone may be another signal.
    const bool onBase = roamingChannel && !away;
    if (onBase && (e.kind == Kind::SlotFound || e.kind == Kind::Burst)) {
        roamWatch.heard(os::nowMs());
        rangeCheck.heard(os::nowMs());
        baseHeardMs = os::nowMs();
    }
    switch (e.kind) {
    case Kind::SlotFound:
        log::debug("dmr") << "timing: " << (e.baseStation ? "repeater" : "direct") << (e.verified ? ", verified" : "")
                          << (e.inverted ? ", inverted" : "") << (e.inboundBusy ? ", busy" : "") << ", next slot "
                          << e.nextSlot;
        if (!timingFound && e.verified) {
            timingFound = true;
            if (onBase && site >= 0) {
                baseTx.dbm = fm->status().rssiDbm;
                baseTx.measured = true;
                noteHeard(site, baseTx.dbm);
            }
            log::info("dmr") << (e.baseStation ? "repeater" : "direct") << " timing found"
                             << (e.inboundBusy ? ", inbound busy" : "");
        } else if (!timingFound && !timingSeen) {
            timingSeen = true;
            log::info("dmr") << (e.baseStation ? "repeater" : "direct") << " timing seen, not yet verified";
        }
        break;
    case Kind::Burst: {
        lastBurstColourCode = e.colourCode;
        if (onBase && (e.voice || e.type != DataType::Idle)) {
            baseTx.idleOnly = false;
        }
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
            log::debug("dmr") << "burst " << lib::hex(e.raw, 2) << ", colour code " << e.colourCode << ": "
                              << bytes.view();
        }
        if (!lc) {
            break;
        }
        if (!e.voice && e.type == DataType::TerminatorWithLc) {
            endCall("ended");
            break;
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
    case Kind::SyncLost:
        timingFound = false;
        timingSeen = false;
        endCall("lost");
        break;
    case Kind::CarrierFound:
        idleLogged = false;
        carrierHeard = true;
        carrierSinceMs = os::nowMs();
        if (onBase) {
            // One already on when the radio came back is not timed from
            // its start.
            baseTx = BaseTransmission{true, os::nowMs() - backOnBaseMs > 60, false, os::nowMs(), 0};
        }
        [[fallthrough]];
    case Kind::CarrierLost:
        log::debug("dmr") << (e.kind == Kind::CarrierFound ? "carrier found" : "carrier lost");
        if (e.kind == Kind::CarrierLost) {
            if (onBase && baseTx.on) {
                baseTransmissionEnded();
            }
            carrierHeard = false;
            // A repeater that went quiet must be woken again before a call.
            timingFound = false;
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

services::RoamingSite siteAt(int i)
{
    sitesLock.lock();
    const services::RoamingSite s = i >= 0 && static_cast<std::size_t>(i) < siteCount ? sites[i] : services::RoamingSite{};
    sitesLock.unlock();
    return s;
}

std::size_t sitesKnown()
{
    sitesLock.lock();
    const std::size_t n = siteCount;
    sitesLock.unlock();
    return n;
}

// The roaming zone's repeater with these frequencies, or -1.
int siteWith(std::uint32_t receive, std::uint32_t transmit)
{
    sitesLock.lock();
    int found = -1;
    for (std::size_t i = 0; i < siteCount && found < 0; ++i) {
        if (sites[i].receiveHz == receive && sites[i].transmitHz == transmit) {
            found = static_cast<int>(i);
        }
    }
    sitesLock.unlock();
    return found;
}

// Works through repeater @p i: its frequencies, and its colour code or the
// channel's.
void useSite(int i)
{
    const services::RoamingSite s = siteAt(i);
    fmChannel.receiveHz = s.receiveHz;
    fmChannel.transmitHz = s.transmitHz;
    transmitHz = s.transmitHz;
    dmrSettings.colourCode =
        s.colourCode != services::RoamingSite::ChannelColourCode ? s.colourCode : channelColourCode;
    dmrRepeater = s.receiveHz != s.transmitHz;
}

// The roaming channel's own frequencies, while no repeater is in use.
void useChannelFrequencies()
{
    fmChannel.receiveHz = channelReceiveHz;
    fmChannel.transmitHz = channelTransmitHz;
    transmitHz = channelTransmitHz;
    dmrSettings.colourCode = channelColourCode;
    dmrRepeater = channelReceiveHz != channelTransmitHz;
}

void showRoaming(bool searching)
{
    if (roamingChannel) {
        ui::post(ui::Roaming{searching, static_cast<std::int8_t>(site), fmChannel.receiveHz, dmrSettings.colourCode});
    }
}

// Messages that come during a search wait for its end; PTT, a channel
// change or another search end it early, as does a full wait list.
bool searchInterrupted()
{
    Message m;
    while (deferredCount < std::size(deferred) && inbox.receive(m, 0)) {
        deferred[deferredCount++] = m;
        if (std::holds_alternative<Ptt>(m) || std::holds_alternative<UseChannel>(m) ||
            std::holds_alternative<RoamNow>(m) || std::holds_alternative<FmReceive>(m) ||
            std::holds_alternative<DmrReceive>(m) || std::holds_alternative<FmIdle>(m) ||
            std::holds_alternative<Restart>(m)) {
            return true;
        }
    }
    return deferredCount == std::size(deferred);
}

// PTT went up while a search ran.
bool pttReleasedMeanwhile()
{
    for (std::size_t i = 0; i < deferredCount; ++i) {
        if (const Ptt *p = std::get_if<Ptt>(&deferred[i]); p != nullptr && !p->down) {
            return true;
        }
    }
    return false;
}

// What a probe found at one repeater.
struct Probe
{
    bool answered = false;
    int rssiDbm = 0;
};

[[gnu::noinline]] void logProbe(int i, const Probe &p, bool woken)
{
    log::info("roam") << "repeater " << i + 1 << ", " << mhz(fmChannel.receiveHz).view() << " MHz: "
                      << (!p.answered ? "no answer" : woken ? "answered the wake-up"
                                                            : "on the air");
    if (p.answered) {
        log::info("roam") << "repeater " << i + 1 << " heard at " << p.rssiDbm << " dBm";
    }
}

[[gnu::noinline]] void logProbeWake(int i, int attempt, bool sent, bool carrier, bool answered)
{
    log::info("roam") << "repeater " << i + 1 << ", wake-up " << attempt << ": "
                      << (!sent ? "not sent" : answered ? "answered"
                                 : carrier              ? "carrier, no timing"
                                                        : "no answer");
}

// Listens on repeater @p i and, when it is quiet, wakes it as a call
// would.
[[gnu::noinline]] Probe probe(int i, std::uint32_t ownId)
{
    useSite(i);
    carrierHeard = false;
    dmrReceive();
    Probe p;
    // A repeater on the air (a beacon, a call) shows its timing without a
    // wake-up.
    awaitDmrEvent(services::dmr::Event::Kind::CarrierFound, ProbeListenMs);
    p.answered = carrierHeard && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, FindSyncMs);
    const bool woken = !p.answered && ownId != 0 && transmitAllowed(transmitHz);
    if (woken) {
        lastWakeMs = os::nowMs();
        services::FmChannel wake = fmChannel;
        wake.continuous = true;
        withBaseband([&](auto &b) {
            for (int t = 0; t < RepeaterWakeTries && !p.answered; ++t) {
                if (fm->transmit(wake, powerLevel) != nullptr) {
                    fm->endTransmit();
                    return;
                }
                showTransmitting(true);
                const bool sent =
                    b.wakeRepeater(dmrSettings, ownId) && awaitDmrEvent(services::dmr::Event::Kind::AllSent, DmrEndMs);
                fm->endTransmit();
                showTransmitting(false);
                const bool listening = sent && b.setDigital(dmrSettings, fmChannel.receiveHz, dmrRepeater) &&
                    fm->receive(fmChannel, 1) == nullptr;
                p.answered = listening && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, RepeaterWakeMs);
                // A carrier without timing yet: a repeater that is slower to
                // start, given as long as one already on the air.
                const bool slow = listening && !p.answered && carrierHeard;
                if (slow) {
                    p.answered = awaitDmrEvent(services::dmr::Event::Kind::SlotFound, FindSyncMs);
                }
                logProbeWake(i, t + 1, sent, slow, p.answered);
            }
        });
    }
    if (p.answered) {
        constexpr int Reads = 3;
        int sum = 0;
        for (int r = 0; r < Reads; ++r) {
            sum += fm->status().rssiDbm;
            os::sleepMs(10);
        }
        p.rssiDbm = sum / Reads;
        noteHeard(i, p.rssiDbm);
    }
    logProbe(i, p, woken);
    return p;
}

// Seconds with one decimal, from milliseconds.
lib::StaticString<12> seconds(std::uint32_t ms)
{
    lib::StaticString<12> t;
    t << ms / 1000 << '.' << static_cast<char>('0' + ms / 100 % 10);
    return t;
}

// The heard list, one repeater a line: level, its age and the beacon
// timing.
[[gnu::noinline, gnu::cold]] void logHeardList()
{
    sitesLock.lock();
    services::roaming::HeardList::Entry entries[services::roaming::HeardList::Capacity];
    const std::size_t n = std::min(heardList.entries().size(), std::size(entries));
    std::copy_n(heardList.entries().begin(), n, entries);
    sitesLock.unlock();
    const std::uint32_t now = os::nowMs();
    for (std::size_t k = 0; k < n; ++k) {
        const services::roaming::HeardList::Entry &e = entries[k];
        const std::uint32_t next = services::roaming::HeardList::nextBeacon(e, now);
        lib::StaticString<48> beacon;
        if (e.periodCs == 0) {
            beacon << "not known";
        } else {
            beacon << "every " << e.periodCs / 100 << '.' << static_cast<char>('0' + e.periodCs % 100 / 10)
                   << static_cast<char>('0' + e.periodCs % 10) << " s, next ";
            if (next == 0) {
                beacon << "not known";
            } else {
                beacon << "in " << seconds(next - now).view() << " s";
            }
        }
        log::info("roam") << "heard: repeater " << e.position + 1 << ", " << mhz(siteAt(e.position).receiveHz).view()
                          << " MHz, " << e.levelDbm << " dBm " << seconds(now - e.measuredMs).view()
                          << " s ago; beacon " << beacon.view();
    }
}

[[gnu::noinline, gnu::cold]] void logRoamingSetup(const services::RoamingSettings &s)
{
    lib::StaticString<40> automatic;
    if (!s.automatic) {
        automatic << "off";
    } else if (s.start == services::RoamingSettings::Start::Periodic) {
        automatic << "every " << s.periodMinutes << " min";
    } else {
        automatic << "after " << s.quietS << " s without the repeater";
    }
    log::info("roam") << sitesKnown() << " repeaters in the roaming zone; automatic " << automatic.view();
    if (s.rangeCheck) {
        log::info("roam") << "range check every " << s.checkIntervalS << " s, out of range after " << s.checkTries
                          << " tries, " << s.outOfRangeNotices << " notices";
    }
}

enum class SearchCause : std::uint8_t
{
    Asked,     // a key, the menu or the console
    Ptt,       // no repeater for a call
    Automatic, // the settings' period or quiet time
};

[[gnu::noinline]] void logSearch(std::size_t count, SearchCause cause)
{
    constexpr const char *Causes[] = {"asked", "for a call", "automatic"};
    log::info("roam") << "searching the roaming zone of " << count << " (" << Causes[static_cast<int>(cause)] << ")";
}

[[gnu::noinline]] void logSearchEnd(int best, bool moved, bool complete)
{
    if (best < 0) {
        log::info("roam") << (complete ? "no repeater answered" : "search cut short");
    } else if (moved) {
        log::info("roam") << "now through repeater " << best + 1;
    } else {
        log::info("roam") << "staying on repeater " << best + 1;
    }
}

// Tries the roaming zone's repeaters, most likely first, until one is good,
// and works through the best that answered. @p skipCurrent leaves out the
// repeater in use, which has just failed to answer. True when one did.
[[gnu::noinline]] bool search(SearchCause cause, bool skipCurrent = false)
{
    const std::size_t count = sitesKnown();
    if (!roamingChannel || count == 0) {
        ui::post(ui::Notice{"No roaming zone"});
        return false;
    }
    if constexpr (!HasBaseband) {
        ui::post(ui::Notice{"No DMR"});
        return false;
    }
    const std::uint32_t ownId = ownIdOverride != 0 ? ownIdOverride : dmrSettings.ownId;
    logSearch(count, cause);
    endCall("left");
    setSpeaker(false);
    searchRunning = true;
    away = true;
    showRoaming(true);
    const int current = skipCurrent ? -1 : site;
    sitesLock.lock();
    const services::roaming::Order order =
        services::roaming::searchOrder(heardList, current, count, restFrom, os::nowMs(), searchList);
    sitesLock.unlock();
    services::roaming::Pick pick(current, GoodDbm, cause == SearchCause::Automatic ? AutomaticMarginDb : 0);
    std::size_t tried = 0;
    bool done = false;
    while (tried < order.count && !done && !searchInterrupted()) {
        const int i = searchList[tried++];
        const Probe p = probe(i, ownId);
        done = pick.tried(i, p.answered, p.rssiDbm);
    }
    if (tried > order.restBegins) {
        restFrom = searchList[tried - 1] + 1u;
    }
    const int best = pick.chosen();
    roamWatch.searched(os::nowMs());
    logSearchEnd(best, best != site, done || tried == order.count);
    if (best >= 0) {
        site = best;
        roamWatch.heard(os::nowMs());
    } else if (tried == order.count && cause != SearchCause::Automatic) {
        ui::post(ui::Notice{"No repeater"});
    }
    if (site >= 0) {
        useSite(site);
    } else {
        useChannelFrequencies();
    }
    receive();
    searchRunning = false;
    away = false;
    baseTx.on = false;
    backOnBaseMs = os::nowMs();
    showRoaming(false);
    return best >= 0;
}

// Back to the repeater in use, or to the channel's own frequencies.
void backToBase()
{
    if (site >= 0) {
        useSite(site);
    } else {
        useChannelFrequencies();
    }
    receive();
    away = false;
    baseTx.on = false;
    backOnBaseMs = os::nowMs();
}

[[gnu::noinline]] void logVisit(const services::roaming::Visit &v, services::roaming::Visits::Found found, int dbm,
    bool interrupted)
{
    using Found = services::roaming::Visits::Found;
    lib::StaticString<32> what;
    if (interrupted) {
        what << "cut short";
    } else if (found == Found::Nothing) {
        what << (v.beaconMs != 0 ? "beacon missed" : "nothing");
    } else {
        what << (found == Found::Start ? "beacon at " : "on the air at ") << dbm << " dBm";
    }
    log::info("roam") << "visit to repeater " << v.position + 1 << (v.sweep ? " (sweep): " : ": ") << what.view();
}

// Listens on another repeater of the zone for a beacon, then comes back.
// Messages that matter (PTT first) end the visit at once.
[[gnu::noinline]] void visit(const services::roaming::Visit &v, services::roaming::Health state)
{
    using Found = services::roaming::Visits::Found;
    away = true;
    useSite(v.position);
    const std::uint8_t colourCode = dmrSettings.colourCode;
    carrierHeard = false;
    timingFound = false;
    timingSeen = false;
    dmrReceive();
    const std::uint32_t arrived = os::nowMs();
    carrierSinceMs = arrived - 1;
    bool interrupted = false;
    auto came = [&] { return static_cast<std::int32_t>(carrierSinceMs - arrived) >= 0; };
    while (!came() && os::nowMs() - arrived < v.dwellMs) {
        if (searchInterrupted()) {
            interrupted = true;
            break;
        }
        awaitDmrEvent(services::dmr::Event::Kind::CarrierFound, 20);
    }
    Found found = Found::Nothing;
    int dbm = 0;
    const std::uint32_t start = carrierSinceMs;
    if (came() && !interrupted && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, 300)) {
        lastBurstColourCode = 0xFF;
        awaitDmrEvent(services::dmr::Event::Kind::Burst, 150);
        if (lastBurstColourCode == colourCode) {
            constexpr int Reads = 3;
            int sum = 0;
            for (int r = 0; r < Reads; ++r) {
                sum += fm->status().rssiDbm;
                os::sleepMs(10);
            }
            dbm = sum / Reads;
            found = start - arrived < 40 ? Found::UnderWay : Found::Start;
        }
    }
    backToBase();
    sitesLock.lock();
    const auto position = static_cast<std::uint8_t>(v.position);
    if (found == Found::Start) {
        heardList.beacon(position, dbm, start);
    } else if (found == Found::UnderWay) {
        heardList.heard(position, dbm, os::nowMs());
    } else if (!interrupted && v.beaconMs != 0) {
        heardList.missed(position);
    }
    sitesLock.unlock();
    if (v.sweep && !interrupted) {
        visits.swept(v.position, found, arrived, state);
    }
    logVisit(v, found, dbm, interrupted);
}

[[gnu::noinline]] void moveSite(int to, services::roaming::Health state)
{
    constexpr const char *States[] = {"unknown", "good", "weak", "failing", "lost"};
    log::info("roam") << "moving from repeater " << site + 1 << " (" << States[static_cast<int>(state)]
                      << ") to repeater " << to + 1;
    site = to;
    lastMoveMs = os::nowMs();
    baseHeardMs = lastMoveMs;
    roamWatch.heard(lastMoveMs);
    useSite(site);
    receive();
    backOnBaseMs = os::nowMs();
    showRoaming(false);
}

// The codeplug's range check: one probe of the repeater in use.
[[gnu::noinline]] void checkRange()
{
    away = true;
    const Probe p = probe(site, ownIdOverride != 0 ? ownIdOverride : dmrSettings.ownId);
    backToBase();
    if (rangeCheck.checked(p.answered, os::nowMs())) {
        log::info("roam") << "repeater " << site + 1 << " out of range";
    }
}

// Roaming between calls: counts the missed beacons of the repeater in use,
// moves to a better repeater as the codeplug's automatic roaming allows,
// and visits the others while the repeater in use is quiet.
void roamStep()
{
    if (!roamingChannel || transmitting || inCall || baseTx.on || deferredCount > 0 || !fm->receiving()) {
        return;
    }
    const std::uint32_t now = os::nowMs();
    const std::size_t zoneSize = sitesKnown();
    sitesLock.lock();
    const services::roaming::HeardList::Entry *own = site >= 0 ? heardList.find(static_cast<std::uint8_t>(site)) : nullptr;
    std::uint32_t ownBeacon = 0;
    bool missed = false;
    if (own != nullptr && own->periodCs != 0 && own->beaconMs != 0) {
        missed = static_cast<std::int32_t>(now - (own->beaconMs + own->periodMs() + MissGraceMs)) > 0;
        if (missed) {
            heardList.missed(static_cast<std::uint8_t>(site));
        }
        ownBeacon = services::roaming::HeardList::nextBeacon(*own, now);
    }
    const services::roaming::Health state = rangeCheck.outOfRange()
        ? services::roaming::Health::Lost
        : services::roaming::health(heardList, site, RoamLevels, now);
    const int to = roamWatch.automatic() && now - lastMoveMs >= MoveGuardMs
        ? services::roaming::moveTo(heardList, site, state, RoamLevels, now)
        : -1;
    sitesLock.unlock();
    if (missed) {
        log::info("roam") << "beacon of repeater " << site + 1 << " missed";
    }
    if (to >= 0) {
        moveSite(to, state);
        return;
    }
    if (rangeCheck.noticeDue(now)) {
        ui::post(ui::Notice{"Out of range"});
    }
    if (site >= 0 && rangeCheck.due(now)) {
        checkRange();
        return;
    }
    if (state == services::roaming::Health::Lost && roamWatch.automatic() && now - lostSearchMs >= LostSearchMs) {
        lostSearchMs = now;
        search(SearchCause::Automatic);
        return;
    }
    if (now - baseHeardMs < BaseQuietMs || power::idleForMs() < UserQuietMs) {
        return;
    }
    sitesLock.lock();
    const services::roaming::Visit v = visits.due(heardList, site, zoneSize, state, now);
    sitesLock.unlock();
    // The repeater in use's own beacon comes first.
    if (v.position < 0 || (ownBeacon != 0 && ownBeacon - now < v.dwellMs + 1000)) {
        return;
    }
    visit(v, state);
}

[[gnu::noinline]] void logWakeTry(int attempt, bool sent, bool answered)
{
    log::info("dmr") << "repeater wake-up " << attempt << ": " << (!sent ? "not sent" : answered ? "answered"
                                                                                                 : "no answer");
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
    const std::uint32_t ownId = ownIdOverride != 0 ? ownIdOverride : dmrSettings.ownId;
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
    // A repeater wake-up goes out before there is any slot timing, so the
    // carrier stays on through it rather than following slots.
    services::FmChannel wake = channel;
    wake.continuous = true;
    const bool waking = dmrRepeater && !timingFound;
    if (waking) {
        lastWakeMs = os::nowMs();
    }
    const char *problem = fm->transmit(waking ? wake : channel, powerLevel);
    if (problem != nullptr) {
        fm->endTransmit();
        logDmrRefused(problem, "TX refused");
        receive();
        return CallStart::Refused;
    }
    bool ok = true;
    // On the air from here, the wake-up included.
    showTransmitting(true);
    withBaseband([&](auto &b) {
        // An idle repeater is woken first, then its timing awaited.
        if (waking) {
            ok = false;
            for (int t = 0; t < RepeaterWakeTries && !ok; ++t) {
                const bool keyed = t == 0 || fm->transmit(wake, powerLevel) == nullptr;
                const bool sent = keyed && b.wakeRepeater(dmrSettings, ownId) &&
                    awaitDmrEvent(services::dmr::Event::Kind::AllSent, DmrEndMs);
                fm->endTransmit();
                ok = sent && b.setDigital(dmrSettings, fmChannel.receiveHz, dmrRepeater) &&
                    fm->receive(fmChannel, 1) == nullptr && awaitDmrEvent(services::dmr::Event::Kind::SlotFound, RepeaterWakeMs);
                logWakeTry(t + 1, sent, ok);
            }
            ok = ok && fm->transmit(channel, powerLevel) == nullptr;
        }
        // timingFound is cleared as soon as the repeater's carrier goes, so
        // it is never stale here.
        ok = ok && b.startDigitalCall(dmrSettings, callLc, channel.transmitHz, dmrRepeater && timingFound, powerLevel);
    });
    if (!ok) {
        fm->endTransmit();
        const bool silent = dmrRepeater && !timingFound;
        // On a roaming channel a silent repeater starts a search instead.
        if (silent && roamingChannel) {
            log::info("dmr") << "PTT: the repeater did not answer";
        } else {
            logDmrRefused(silent ? "the repeater did not answer" : "the baseband refused",
                silent ? "No repeater" : "TX refused");
        }
        showTransmitting(false);
        receive();
        return silent ? CallStart::NoRepeater : CallStart::Refused;
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

// A call on a roaming channel: a search first while no repeater is known,
// and once more when the one in use does not answer.
void startRoamingCall()
{
    bool searched = false;
    if (site < 0) {
        searched = true;
        if (!search(SearchCause::Ptt) || pttReleasedMeanwhile()) {
            return;
        }
    }
    for (;;) {
        if (startDigitalTransmit(callChannel()) != CallStart::NoRepeater) {
            return;
        }
        if (searched) {
            ui::post(ui::Notice{"No repeater"});
            return;
        }
        searched = true;
        if (!search(SearchCause::Ptt, true) || pttReleasedMeanwhile()) {
            return;
        }
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
        } else if (roamingChannel) {
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
                b.setDigital(dmrSettings, fmChannel.receiveHz, dmrRepeater);
            } else {
                basebandAnalogReceive();
            }
        }
    });
    chipsReady.store(true);
    log::info("radio") << "ready";
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
                       roamingChannel = false;
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
                       roamingChannel = false;
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
                       channelReceiveHz = c.receiveHz;
                       channelTransmitHz = c.transmitHz;
                       channelColourCode = c.dmr.colourCode;
                       sitesLock.lock();
                       if (sitesChanged) {
                           sitesChanged = false;
                           site = -1;
                           heardList.clear();
                           restFrom = 0;
                       }
                       sitesLock.unlock();
                       // A roaming channel keeps the repeater in use; else it
                       // starts on its own frequencies when they are one of the
                       // zone's repeaters.
                       roamingChannel = roamingOn && c.digital() && c.roaming && sitesKnown() > 0;
                       if (roamingChannel && site < 0) {
                           site = siteWith(c.receiveHz, c.transmitHz);
                       }
                       if (roamingChannel && site >= 0) {
                           useSite(site);
                       }
                       // The quiet time counts from the change.
                       roamWatch.heard(os::nowMs());
                       baseHeardMs = os::nowMs();
                       rangeCheck.heard(os::nowMs());
                       baseTx.on = false;
                       receive();
                       showRoaming(false);
                   },
                   [](const RoamNow &) {
                       if (!roamingChannel) {
                           log::info("roam") << "not a roaming channel";
                           ui::post(ui::Notice{"Not roaming"});
                           return;
                       }
                       search(SearchCause::Asked);
                   },
                   [](const RoamingOn &r) {
                       roamingOn = r.on;
                       log::info("roam") << "roaming " << (r.on ? "on" : "off");
                   },
                   [](const RoamAutomatic &r) {
                       roamWatch.setAutomatic(r.on);
                       log::info("roam") << "automatic " << (r.on ? "on" : "off") << " for this session";
                   },
                   [](const RoamingSetup &r) {
                       roamWatch.configure(r.settings, os::nowMs());
                       rangeCheck.configure(r.settings, os::nowMs());
                       logRoamingSetup(r.settings);
                   },
                   [](const RoamStatus &) {
                       log::info("roam") << (roamingOn ? "on, " : "off, ")
                                         << (roamingChannel ? "roaming channel" : "not a roaming channel") << ", "
                                         << sitesKnown() << " repeaters, in use: " << site + 1 << " ("
                                         << mhz(fmChannel.receiveHz).view() << " MHz, colour code "
                                         << dmrSettings.colourCode << "), automatic "
                                         << (roamWatch.automatic() ? "on" : "off");
                       logHeardList();
                   },
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
        if (roamingChannel && !transmitting && !inCall && fm->receiving() && roamWatch.due(os::nowMs())) {
            search(SearchCause::Automatic);
        }
        roamStep();
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

void roamingView(RoamingView &out)
{
    sitesLock.lock();
    out.roaming = roamingChannel;
    out.searching = searchRunning;
    out.site = site;
    out.health = services::roaming::health(heardList, site, RoamLevels, os::nowMs());
    out.count = static_cast<std::uint8_t>(heardList.entries().size());
    for (std::size_t k = 0; k < out.count; ++k) {
        out.entries[k] = heardList.entries()[k];
        const std::uint8_t i = out.entries[k].position;
        out.receiveHz[k] = i < siteCount ? sites[i].receiveHz : 0;
    }
    sitesLock.unlock();
}

void setRoamingSites(lib::FnRef<std::size_t(std::span<services::RoamingSite>)> fill)
{
    sitesLock.lock();
    siteCount = std::min(fill(sites), std::size(sites));
    sitesChanged = true;
    sitesLock.unlock();
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

bool ready()
{
    return chipsReady.load();
}

bool post(const Message &message)
{
    return fm != nullptr && inbox.send(message);
}

} // namespace dmm::app::radio
