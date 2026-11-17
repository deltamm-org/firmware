// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "aprs.h"

#include "gps.h"
#include "radio.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/lib/strings.h"
#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/afsk_demodulator.h"
#include "dmm/services/aprs.h"
#include "dmm/services/ax25.h"
#include "dmm/services/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>

namespace dmm::app::aprs {

namespace {

namespace log = services::log;
namespace ax25 = services::ax25;
using services::AprsSettings;
using services::GpsSchedule;

// Room for the deepest path, a packet decoded down to the heard list and
// the log (about 1.7 KB with the kernel's calls and an interrupt), and a
// margin.
os::Task<640> aprsTask;
// Above the ui, gps and console tasks: a long screen redraw or codeplug
// write must not keep the task from the sampler's ring.
constexpr unsigned AprsPriority = 5;
os::Queue<Message, 4> inbox;

// The destination names the software (an experimental APZ tocall).
constexpr std::string_view Destination = "APZDMM";
// A beacon sends a position at most this old, and waits this long for one.
constexpr std::uint32_t MaxFixAgeMs = 2 * 60'000;
constexpr os::Ticks WaitForFixMs = 3 * 60'000;
// How often a waiting beacon looks again.
constexpr os::Ticks RetryMs = 5000;
constexpr os::Ticks IdleMs = 1000;
// Fewer satellites than this give a poor altitude, which is left out.
constexpr std::uint8_t AltitudeSatellites = 5;

std::optional<AprsSettings> station;
std::uint16_t intervalS = 0;
services::PowerLevel power = services::PowerLevel::Low;
os::Ticks nextBeaconAt = 0;
// A beacon waiting for a position or for the radio, since when.
bool pending = false;
// The waiting beacon goes out on the current channel (console test).
bool pendingHere = false;
os::Ticks pendingSince = 0;
os::Ticks nextTry = 0;
bool waitingForFix = false;

// Too big for the stack. The task builds beacons and reads received
// packets in turn, so they share the frame and the position.
ax25::Frame frame;
services::aprs::Position position;

// Decoding the received audio: in APRS mode it starts once the radio has
// listened on the mode's frequency for a moment, and stops as soon as it
// does not (a transmission).
board::AudioSampler *sampler = nullptr;
services::AfskDemodulator demodulator;
bool modeOn = false;
std::uint32_t modeHz = 0;
DecodeMode decodeMode = DecodeMode::On;
bool decoding = false;
bool listenSeen = false;
os::Ticks listenSince = 0;
std::uint32_t lostSeen = 0;
std::uint32_t lostTotal = 0;
// The longest time between two reads of the sampler and when it ended,
// and the longest one pass of decoding took.
os::Ticks lastReadAt = 0;
os::Ticks longestGapMs = 0;
os::Ticks longestGapAt = 0;
os::Ticks longestPassMs = 0;
constexpr os::Ticks DecodeSettleMs = 1000;
// How often the sampler is read while the radio listens for packets.
constexpr os::Ticks ReceivePollMs = 20;
using services::AfskDemodulator;

// For the home screen, read by other tasks.
std::atomic<bool> decodingShown{false};
std::atomic<std::uint32_t> packetsHeard{0};
std::atomic<services::aprs::BeaconStatus::State> beaconShown{services::aprs::BeaconStatus::State::NoStation};
std::atomic<os::Ticks> nextBeaconShown{0};

// Guards what other tasks hand over or read: the station from the
// codeplug, and the heard list, newest first.
os::Mutex lock;
std::optional<AprsSettings> newStation;
std::array<HeardStation, MaxHeard> heardList{};
std::size_t heardTotal = 0;

const char *decodeModeText()
{
    return decodeMode == DecodeMode::Off ? "off" : decodeMode == DecodeMode::On ? "in APRS mode"
                                                                                : "on any analog channel";
}

lib::StaticString<12> frequencyText(std::uint32_t hz)
{
    lib::StaticString<12> s;
    s << hz / 1'000'000 << '.' << lib::zeroPadded(hz / 1000 % 1000, 3);
    return s;
}

void setGpsNeed(std::uint32_t intervalMs)
{
    gps::post(gps::Need{GpsSchedule::Client::Aprs, intervalMs});
}

void report()
{
    log::info("aprs") << "APRS mode " << (modeOn ? "on" : "off");
    if (sampler != nullptr) {
        log::info("aprs") << "decoding " << decodeModeText() << (decoding ? ", running" : "") << ": "
                          << demodulator.stats().passed << " frames";
    }
    if (!station) {
        log::info("aprs") << "no station: the codeplug has no APRS callsign";
        return;
    }
    const AprsSettings &s = *station;
    constexpr const char *Powers[] = {"low", "mid", "high", "turbo"};
    log::info("aprs") << s.callsign << '-' << s.ssid << ", path " << (s.path[0] != '\0' ? s.path : "none") << ", symbol "
                      << s.symbolTable << s.symbol << ", "
                      << (s.frequencyHz != 0 ? frequencyText(s.frequencyHz).view() : std::string_view("no frequency"))
                      << (s.frequencyHz != 0 ? " MHz " : " ") << (s.wide ? "wide" : "narrow") << ", power " << Powers[static_cast<int>(power) & 3];
    log::info("aprs") << "comment \"" << s.comment << "\"";
    auto line = log::info("aprs");
    if (intervalS == 0) {
        line << "beacons off";
    } else {
        line << "beacon every " << intervalS << " s, next in "
             << static_cast<std::int32_t>(nextBeaconAt - os::nowMs()) / 1000 << " s";
    }
    if (pending) {
        line << "; one waiting " << (waitingForFix ? "for a position" : "for the radio");
    }
}

// Our position from @p f, into position.
void setOurPosition(const services::GpsFix &f)
{
    services::aprs::Position &p = position;
    p = {};
    p.latitudeE7 = f.latitudeE7;
    p.longitudeE7 = f.longitudeE7;
    p.symbolTable = station->symbolTable;
    p.symbol = station->symbol;
    // 1 knot = 1.852 km/h; course and speed only while moving.
    const std::uint32_t knots = (f.speedDeciKmh * 10u + 926) / 1852;
    if (knots >= 2) {
        p.hasCourse = true;
        const auto degrees = static_cast<std::uint16_t>((f.courseDeciDeg + 5) / 10 % 360);
        p.courseDeg = degrees == 0 ? 360 : degrees;
        p.speedKnots = static_cast<std::uint16_t>(knots);
    }
    if (f.satellitesUsed >= AltitudeSatellites) {
        p.hasAltitude = true;
        p.altitudeM = (f.altitudeDm + (f.altitudeDm < 0 ? -5 : 5)) / 10;
    }
    p.comment << station->comment;
}

// The frame for a position report from @p fix; false if the station's
// callsign or path does not make one.
bool buildFrame(const services::GpsFix &fix)
{
    lib::StaticString<10> call;
    call << station->callsign << '-' << station->ssid;
    const auto source = ax25::parseAddress(call.view());
    if (!source) {
        return false;
    }
    frame.source = *source;
    frame.destination = *ax25::parseAddress(Destination);
    frame.path.clear();
    std::string_view path(station->path);
    while (!path.empty()) {
        const std::size_t comma = path.find(',');
        const std::string_view hop(path.data(), comma == std::string_view::npos ? path.size() : comma);
        const auto a = ax25::parseAddress(hop);
        if (!a || !frame.path.pushBack(*a)) {
            return false;
        }
        path.remove_prefix(comma == std::string_view::npos ? path.size() : comma + 1);
    }
    setOurPosition(fix);
    services::aprs::positionReport(position, false, frame.info);
    return true;
}

void endPending()
{
    pending = false;
    waitingForFix = false;
    setGpsNeed(static_cast<std::uint32_t>(intervalS) * 1000);
}

// Tries to queue the waiting beacon.
void tryBeacon()
{
    const os::Ticks now = os::nowMs();
    if (!station) {
        log::warning("aprs") << "beacon not sent: the codeplug has no APRS callsign";
        ui::post(ui::AprsProblem{ui::AprsProblem::Kind::NoCallsign});
        pending = false;
        return;
    }
    if (!pendingHere && station->frequencyHz == 0) {
        log::warning("aprs") << "beacon not sent: the codeplug has no APRS frequency";
        ui::post(ui::AprsProblem{ui::AprsProblem::Kind::NoFrequency});
        endPending();
        return;
    }
    const std::optional<services::GpsFix> fix = gps::latestFix(MaxFixAgeMs);
    if (!fix) {
        if (now - pendingSince >= WaitForFixMs) {
            log::warning("aprs") << "beacon not sent: no GPS position within " << WaitForFixMs / 60'000 << " min";
            ui::post(ui::AprsProblem{ui::AprsProblem::Kind::NoFix});
            endPending();
            return;
        }
        if (!waitingForFix) {
            // Positions every second until one comes.
            waitingForFix = true;
            setGpsNeed(1000);
            log::info("aprs") << "beacon waits for a GPS position";
        }
        nextTry = now + RetryMs;
        return;
    }
    if (!buildFrame(*fix)) {
        log::warning("aprs") << "beacon not sent: the callsign or path is not valid";
        ui::post(ui::AprsProblem{ui::AprsProblem::Kind::NoCallsign});
        endPending();
        return;
    }
    const radio::PacketChannel channel{pendingHere ? 0u : station->frequencyHz, station->wide, power};
    if (!radio::sendPacket(frame, channel)) {
        // The radio has a packet already; look again soon.
        if (now - pendingSince >= WaitForFixMs) {
            ui::post(ui::AprsProblem{ui::AprsProblem::Kind::Busy});
            endPending();
        } else {
            nextTry = now + RetryMs;
        }
        return;
    }
    auto line = log::info("aprs");
    line << "beacon ";
    ax25::writeText(line, frame);
    endPending();
}

void startBeacon(bool here = false)
{
    if (pending) {
        return;
    }
    pending = true;
    pendingHere = here;
    pendingSince = os::nowMs();
    nextTry = pendingSince;
}

void copyText(char *to, std::size_t bytes, std::string_view text)
{
    std::size_t n = 0;
    for (char ch : text) {
        if (n == bytes) {
            break;
        }
        to[n++] = ch >= ' ' && ch <= '~' ? ch : ' ';
    }
    to[n] = '\0';
}

// Puts what @p frame says into the list, at the top.
void remember()
{
    HeardStation h;
    copyText(h.call, HeardStation::CallBytes, ax25::addressText(frame.source).view());
    for (const ax25::Address &a : frame.path) {
        if (a.repeated) {
            ax25::Address plain = a;
            plain.repeated = false;
            copyText(h.via, HeardStation::CallBytes, ax25::addressText(plain).view());
        }
    }
    const std::optional<services::aprs::Position> read = services::aprs::readPosition(frame);
    if (read) {
        position = *read;
        const services::aprs::Position *p = &position;
        h.hasPosition = true;
        h.latitudeE7 = p->latitudeE7;
        h.longitudeE7 = p->longitudeE7;
        h.symbolTable = p->symbolTable;
        h.symbol = p->symbol;
        h.hasAltitude = p->hasAltitude;
        h.altitudeM = p->altitudeM;
        h.hasCourse = p->hasCourse;
        h.courseDeg = p->courseDeg;
        h.speedKnots = p->speedKnots;
        copyText(h.text, HeardStation::TextBytes, p->comment.view());
    } else {
        std::string_view info = frame.info.view();
        if (!info.empty() && info[0] == '>') {
            info.remove_prefix(1);
        }
        copyText(h.text, HeardStation::TextBytes, info);
    }
    h.heardAtMs = os::nowMs();

    lock.lock();
    std::size_t at = heardTotal;
    for (std::size_t i = 0; i < heardTotal; ++i) {
        if (std::strcmp(heardList[i].call, h.call) == 0) {
            at = i;
            break;
        }
    }
    if (at < heardTotal) {
        h.packets = static_cast<std::uint16_t>(heardList[at].packets + 1);
        // A packet without a position keeps the last one heard.
        if (!h.hasPosition && heardList[at].hasPosition) {
            const HeardStation &old = heardList[at];
            h.hasPosition = true;
            h.latitudeE7 = old.latitudeE7;
            h.longitudeE7 = old.longitudeE7;
            h.symbolTable = old.symbolTable;
            h.symbol = old.symbol;
        }
    } else {
        h.packets = 1;
        at = heardTotal < MaxHeard ? heardTotal++ : MaxHeard - 1;
    }
    for (std::size_t i = at; i > 0; --i) {
        heardList[i] = heardList[i - 1];
    }
    heardList[0] = h;
    lock.unlock();
}

// A frame with a right check from the decoder.
void heardFrame(std::span<const std::uint8_t> bytes, const AfskDemodulator::FrameInfo &info)
{
    if (!ax25::decode(bytes, frame)) {
        return;
    }
    packetsHeard.store(packetsHeard.load() + 1);
    remember();
    auto line = log::info("aprs");
    line << "heard ";
    if (info.repaired) {
        line << "(repaired) ";
    }
    ax25::writeText(line, frame);
    ui::post(ui::AprsHeard{});
}

// The radio listens where the decoder works: on the mode's frequency, or
// anywhere for a test.
bool onDecodingFrequency()
{
    const std::uint32_t hz = radio::analogListeningHz();
    return hz != 0 && (decodeMode == DecodeMode::AnyChannel || (modeOn && hz == modeHz));
}

void stopDecoding()
{
    if (!decoding) {
        return;
    }
    sampler->stop();
    decoding = false;
    log::info("aprs") << "decoding off";
}

// Starts or stops decoding as the radio and the settings say.
void updateDecoding()
{
    const bool wanted = decodeMode != DecodeMode::Off && sampler != nullptr && onDecodingFrequency();
    if (!wanted) {
        listenSeen = false;
        stopDecoding();
        return;
    }
    const os::Ticks now = os::nowMs();
    if (decoding) {
        return;
    }
    if (!listenSeen) {
        listenSeen = true;
        listenSince = now;
        return;
    }
    if (now - listenSince < DecodeSettleMs) {
        return;
    }
    if (!sampler->start(AfskDemodulator::SampleRateHz)) {
        decodeMode = DecodeMode::Off;
        log::warning("aprs") << "decoding off: the audio cannot be sampled at " << AfskDemodulator::SampleRateHz << " Hz";
        return;
    }
    demodulator.reset();
    lostSeen = sampler->lostSamples();
    lastReadAt = now;
    decoding = true;
    log::info("aprs") << "decoding on at " << frequencyText(radio::analogListeningHz()).view() << " MHz";
}

[[gnu::noinline, gnu::cold]] void logLost(std::uint32_t samples, os::Ticks gapMs)
{
    log::info("aprs") << samples << " samples lost: no read for " << gapMs << " ms";
}

// Decodes the samples taken since the last poll. Kept apart from the
// task's other work, so a frame's path down to the log stays short.
[[gnu::noinline]] void pollSampler()
{
    static std::uint16_t chunk[64];
    const os::Ticks start = os::nowMs();
    const os::Ticks gap = start - lastReadAt;
    lastReadAt = start;
    if (gap > longestGapMs) {
        longestGapMs = gap;
        longestGapAt = start;
    }
    for (;;) {
        const std::size_t n = sampler->read(chunk);
        const std::uint32_t lost = sampler->lostSamples();
        if (lost != lostSeen) {
            // A gap breaks the frame in progress.
            logLost(lost - lostSeen, gap);
            lostTotal += lost - lostSeen;
            lostSeen = lost;
            demodulator.reset();
        }
        if (n == 0) {
            break;
        }
        demodulator.process(std::span<const std::uint16_t>(chunk, n), heardFrame);
    }
    longestPassMs = std::max(longestPassMs, os::nowMs() - start);
}

void receiveReport()
{
    log::info("aprs") << "APRS mode " << (modeOn ? "on" : "off") << "; decoding " << decodeModeText()
                      << (decoding ? ", running" : ", not running") << ", repair " << (demodulator.repair() ? "on" : "off");
    const AfskDemodulator::Stats &st = demodulator.stats();
    log::info("aprs") << "frames: " << st.passed << " (" << st.repaired << " repaired); "
                      << st.samples / AfskDemodulator::SampleRateHz << " s of audio, " << lostTotal
                      << " samples lost, " << st.duplicates << " copies between slicers";
    log::info("aprs") << "reads: longest gap " << longestGapMs << " ms (at " << longestGapAt / 1000
                      << " s), longest decoding pass " << longestPassMs << " ms";
    auto line = log::info("aprs");
    line << "slicers (dB right/failed):";
    for (std::size_t i = 0; i < AfskDemodulator::Slicers; ++i) {
        const int db = AfskDemodulator::MarkGainDb[i];
        line << ' ' << (db < 0 ? "-" : "+") << (db < 0 ? -db : db) << ' ' << st.frames[i] << '/' << st.failed[i];
    }
}

[[gnu::noinline]] void handle(const Message &message)
{
    lib::visit(lib::Overload{
                   [](const BeaconInterval &b) {
                       intervalS = b.seconds;
                       // The first beacon goes as soon as there is a position.
                       nextBeaconAt = os::nowMs();
                       if (!pending) {
                           setGpsNeed(static_cast<std::uint32_t>(intervalS) * 1000);
                       }
                       log::info("aprs") << (intervalS == 0 ? "beacons off" : "beacons on");
                   },
                   [](const BeaconPower &p) { power = p.level; },
                   [](const BeaconNow &b) { startBeacon(b.here); },
                   [](const Report &) { report(); },
                   [](const ModeOn &m) {
                       modeOn = m.on;
                       modeHz = m.hz;
                       log::info("aprs") << "APRS mode " << (m.on ? "on at " : "off")
                                         << (m.on ? frequencyText(m.hz).view() : std::string_view())
                                         << (m.on ? " MHz" : "");
                   },
                   [](const Decode &m) {
                       decodeMode = m.mode;
                       log::info("aprs") << "decoding " << decodeModeText();
                   },
                   [](const RepairFrames &r) {
                       demodulator.setRepair(r.on);
                       log::info("aprs") << "repair " << (r.on ? "on" : "off");
                   },
                   [](const ReceiveReport &) { receiveReport(); },
               },
        message);
}

// Takes a station setStation() handed over.
void takeStation()
{
    lock.lock();
    std::optional<AprsSettings> s = newStation;
    newStation.reset();
    lock.unlock();
    if (!s || s->callsign[0] == '\0') {
        return;
    }
    station = s;
    power = s->power;
    intervalS = s->beaconIntervalS;
    nextBeaconAt = os::nowMs() + static_cast<os::Ticks>(intervalS) * 1000;
    setGpsNeed(static_cast<std::uint32_t>(intervalS) * 1000);
    report();
}

// What the home screen shows of the beacon, for other tasks.
void showBeacon()
{
    using State = services::aprs::BeaconStatus::State;
    const State state = !station   ? State::NoStation
        : pending && waitingForFix ? State::WaitingForFix
        : pending                  ? State::Queued
        : intervalS == 0           ? State::Off
                                   : State::Next;
    beaconShown.store(state);
    nextBeaconShown.store(nextBeaconAt);
    decodingShown.store(decoding);
}

void aprsMain(void *)
{
    while (!os::stopRequested()) {
        Message m;
        const bool listening = decoding || modeOn || decodeMode == DecodeMode::AnyChannel;
        if (inbox.receive(m, listening ? ReceivePollMs : pending ? RetryMs
                                                                 : IdleMs)) {
            handle(m);
        }
        takeStation();
        updateDecoding();
        if (decoding) {
            pollSampler();
        }
        const os::Ticks now = os::nowMs();
        if (intervalS != 0 && static_cast<std::int32_t>(now - nextBeaconAt) >= 0) {
            nextBeaconAt = now + static_cast<os::Ticks>(intervalS) * 1000;
            startBeacon();
        }
        if (pending && static_cast<std::int32_t>(now - nextTry) >= 0) {
            tryBeacon();
        }
        showBeacon();
    }
}

// Packet settings the console last set.
std::uint16_t leadMs = 300;
std::uint8_t levelPercent = 5;

} // namespace

void start(board::AudioSampler *s)
{
    sampler = s;
    aprsTask.start("aprs", AprsPriority, aprsMain, nullptr);
}

void setStation(const AprsSettings &settings)
{
    lock.lock();
    newStation = settings;
    lock.unlock();
}

bool heardStation(std::size_t index, HeardStation &out)
{
    lock.lock();
    const bool found = index < heardTotal;
    if (found) {
        out = heardList[index];
    }
    lock.unlock();
    return found;
}

bool heardStation(std::string_view call, HeardStation &out)
{
    lock.lock();
    bool found = false;
    for (std::size_t i = 0; i < heardTotal && !found; ++i) {
        if (call == heardList[i].call) {
            out = heardList[i];
            found = true;
        }
    }
    lock.unlock();
    return found;
}

std::size_t heardCount()
{
    lock.lock();
    const std::size_t n = heardTotal;
    lock.unlock();
    return n;
}

bool post(const Message &message)
{
    return inbox.send(message);
}

ReceiveView receiveView()
{
    return {decodingShown.load(), packetsHeard.load()};
}

services::aprs::BeaconStatus beaconStatus()
{
    services::aprs::BeaconStatus b;
    b.state = beaconShown.load();
    const auto left = static_cast<std::int32_t>(nextBeaconShown.load() - os::nowMs());
    b.nextInS = left > 0 ? static_cast<std::uint32_t>(left + 999) / 1000 : 0;
    return b;
}

const char *consoleHelp()
{
    return "aprs, aprs mode on|off, aprs send [here], aprs every <s> (0 = off), aprs lead <ms>, aprs level <%>";
}

const char *receiveHelp()
{
    return "aprs rx, aprs rx decode on|off|any, aprs rx repair on|off";
}

bool runCommand(std::string_view command)
{
    if (command == "aprs") {
        post(Report{});
        return true;
    }
    if (!lib::startsWith(command, "aprs ")) {
        return false;
    }
    std::string_view rest(command.data() + 5, command.size() - 5);
    const std::string_view word = lib::takeWord(rest);
    if (word == "rx") {
        const std::string_view what = lib::takeWord(rest);
        const std::string_view value = lib::takeWord(rest);
        bool posted = true;
        if (what.empty()) {
            posted = post(ReceiveReport{});
        } else if (what == "decode" && rest.empty() && (value == "on" || value == "off" || value == "any")) {
            posted = post(Decode{value == "on" ? DecodeMode::On : value == "off" ? DecodeMode::Off
                                                                                 : DecodeMode::AnyChannel});
        } else if (what == "repair" && rest.empty() && (value == "on" || value == "off")) {
            posted = post(RepairFrames{value == "on"});
        } else {
            log::info("console") << receiveHelp();
        }
        if (!posted) {
            log::info("console") << "busy";
        }
        return true;
    }
    if (word == "mode" && (rest == "on" || rest == "off")) {
        if (!ui::post(ui::SetMode{rest == "on" ? services::DedicatedMode::Aprs : services::DedicatedMode::Off})) {
            log::info("console") << "busy";
        }
        return true;
    }
    if (word == "send" && (rest.empty() || rest == "here")) {
        post(BeaconNow{rest == "here"});
        return true;
    }
    const std::optional<std::int32_t> n = lib::parseNumber(rest, 10);
    if (word == "every" && n && *n <= 65535) {
        post(BeaconInterval{static_cast<std::uint16_t>(*n)});
    } else if (word == "lead" && n && *n >= 10 && *n <= 2000) {
        leadMs = static_cast<std::uint16_t>(*n);
        radio::post(radio::PacketSettings{leadMs, levelPercent});
    } else if (word == "level" && n && *n >= 1 && *n <= 100) {
        levelPercent = static_cast<std::uint8_t>(*n);
        radio::post(radio::PacketSettings{leadMs, levelPercent});
    } else {
        return false;
    }
    return true;
}

} // namespace dmm::app::aprs
