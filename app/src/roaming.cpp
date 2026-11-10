// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "roaming.h"

#include "radio.h"
#include "radio_internal.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/os/os.h"
#include "dmm/services/log.h"
#include "dmm/services/roaming.h"

#include <algorithm>

namespace dmm::app::radio::roam {

namespace {

namespace log = services::log;
namespace roaming = services::roaming;
using namespace internal;

// The roaming zone's repeaters and the heard list, which the UI task reads,
// and a new zone the UI task chose: all under sitesLock. Only this task
// takes a new zone, at the next use of a channel, so that the positions it
// holds meanwhile stay those of the zone it works through.
os::Mutex sitesLock;
services::RoamingSite sites[services::CodeplugSource::MaxRoamingSites];
std::size_t siteCount = 0;
services::RoamingSite newSites[services::CodeplugSource::MaxRoamingSites];
std::size_t newSiteCount = 0;
bool sitesChanged = false;
roaming::HeardList heardList;

// A roaming channel in use, and the repeater in use (-1: none yet). A
// roaming channel takes its frequencies and colour code from that repeater,
// unless the repeater leaves the colour code to the channel; with none in
// use, the channel's own.
bool roamingChannel = false;
int site = -1;
std::uint32_t channelReceiveHz = 0;
std::uint32_t channelTransmitHz = 0;
std::uint8_t channelColourCode = 1;
// Auto roaming (the codeplug's, then the menu's): whether the radio moves
// to a better repeater by itself.
bool automatic = false;

constexpr roaming::Levels Levels{};
roaming::Activity activity;
roaming::Listening listener;
// A search runs; tuned off the repeater in use (a search or a check).
bool searching = false;
bool away = false;
// The screen shows that no repeater answered.
bool shownLost = false;

// A probe listens this long for a repeater already on the air before it
// wakes it.
constexpr os::Ticks ProbeListenMs = 150;
// A check waits this long for a carrier, then for slot timing, then for a
// burst.
constexpr os::Ticks CheckCarrierMs = 80;
constexpr os::Ticks CheckTimingMs = 300;
constexpr os::Ticks CheckBurstMs = 150;

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

// The zone position matching @p match, or -1.
template <class F>
int findSite(F &&match)
{
    sitesLock.lock();
    int found = -1;
    for (std::size_t i = 0; i < siteCount && found < 0; ++i) {
        if (match(sites[i])) {
            found = static_cast<int>(i);
        }
    }
    sitesLock.unlock();
    return found;
}

std::uint8_t colourCodeOf(const services::RoamingSite &s)
{
    return s.colourCode != services::RoamingSite::ChannelColourCode ? s.colourCode : channelColourCode;
}

// The floor repeater @p i's level is judged against: the radio model's
// reference for its band.
int floorAt(int i)
{
    return board::roamingFloorDbm(siteAt(i).receiveHz);
}

// Repeater @p i heard at @p dbm.
void noteHeard(int i, int dbm)
{
    const int floorDbm = floorAt(i); // takes sitesLock itself
    sitesLock.lock();
    heardList.heard(static_cast<std::uint8_t>(i), dbm, floorDbm, os::nowMs());
    sitesLock.unlock();
}

// Repeater @p i's level in the heard list, in dB above its floor: the
// readings of it averaged; @p fallbackDb when it is not listed.
int heardLevelDb(int i, int fallbackDb)
{
    sitesLock.lock();
    const roaming::HeardList::Entry *e = heardList.find(static_cast<std::uint8_t>(i));
    const int db = e != nullptr ? e->levelDb() : fallbackDb;
    sitesLock.unlock();
    return db;
}

roaming::Health healthNow()
{
    sitesLock.lock();
    const roaming::Health h = roaming::health(heardList, site, Levels, os::nowMs());
    sitesLock.unlock();
    return h;
}

// Works through repeater @p i, or the channel's own frequencies for -1.
void tuneTo(int i)
{
    if (i < 0) {
        useRepeater(channelReceiveHz, channelTransmitHz, channelColourCode);
    } else {
        const services::RoamingSite s = siteAt(i);
        useRepeater(s.receiveHz, s.transmitHz, colourCodeOf(s));
    }
}

void showRoaming(bool searchShown)
{
    if (roamingChannel) {
        shownLost = !searchShown && healthNow() == roaming::Health::Lost;
        ui::post(ui::Roaming{searchShown, shownLost, static_cast<std::int8_t>(site), tunedHz(), tunedColourCode()});
    }
}

// Back on the repeater in use, or on the channel's own frequencies.
void backToBase()
{
    tuneTo(site);
    backHere();
    away = false;
}

// A new repeater in use: nothing is known of its transmissions yet.
void useSite(int i)
{
    site = i;
    sitesLock.lock();
    heardList.keep(i);
    sitesLock.unlock();
    activity.clear();
    backToBase();
    showRoaming(false);
}

[[gnu::noinline]] void logNewZone(std::size_t count, int was, int now)
{
    auto line = log::info("roam");
    line << "new roaming zone of " << count << " repeaters";
    if (was >= 0) {
        line << "; repeater in use " << was + 1;
        if (now >= 0) {
            line << " is now " << now + 1;
        } else {
            line << " not in it";
        }
    }
}

// Takes the zone the UI task chose, if any. A repeater both zones have
// keeps what was heard of it under its new position, and stays in use; a
// pass starts afresh.
void takeNewZone()
{
    sitesLock.lock();
    if (!sitesChanged) {
        sitesLock.unlock();
        return;
    }
    std::int16_t moved[services::CodeplugSource::MaxRoamingSites];
    bool taken[services::CodeplugSource::MaxRoamingSites] = {};
    for (std::size_t k = 0; k < siteCount; ++k) {
        moved[k] = -1;
        const services::RoamingSite &s = sites[k];
        for (std::size_t j = 0; j < newSiteCount && moved[k] < 0; ++j) {
            const services::RoamingSite &n = newSites[j];
            if (!taken[j] && n.receiveHz == s.receiveHz && n.transmitHz == s.transmitHz &&
                n.colourCode == s.colourCode) {
                taken[j] = true;
                moved[k] = static_cast<std::int16_t>(j);
            }
        }
    }
    const int was = site;
    heardList.renumber(std::span<const std::int16_t>(moved, siteCount));
    site = site >= 0 && static_cast<std::size_t>(site) < siteCount ? moved[site] : -1;
    std::copy_n(newSites, newSiteCount, sites);
    siteCount = newSiteCount;
    sitesChanged = false;
    sitesLock.unlock();
    listener.restartPass();
    logNewZone(siteCount, was, site);
}

// What a probe found at one repeater.
struct Probe
{
    bool answered = false;
    bool woken = false;
    int rssiDbm = 0; // the level read
    int levelDb = 0; // its level in the heard list since, above the floor
};

[[gnu::noinline]] void logProbe(int i, const Probe &p)
{
    log::info("roam") << "repeater " << i + 1 << ", " << mhz(tunedHz()).view() << " MHz: "
                      << (!p.answered ? "no answer" : p.woken ? "answered the wake-up"
                                                              : "on the air");
    if (p.answered) {
        log::info("roam") << "repeater " << i + 1 << " heard at " << p.rssiDbm << " dBm, level "
                          << p.levelDb + floorAt(i) << " dBm";
    }
}

// Listens on repeater @p i and, when it is quiet, wakes it as a call
// would, @p tries times at most.
[[gnu::noinline]] Probe probe(int i, int tries)
{
    tuneTo(i);
    Probe p;
    // A repeater on the air (a beacon, a call) shows its timing without a
    // wake-up.
    p.answered = onAirHere(ProbeListenMs);
    p.woken = !p.answered && ownDmrId() != 0 && transmitAllowed(siteAt(i).transmitHz);
    if (p.woken) {
        const char *problem = nullptr;
        p.answered = wakeRepeater(tries, problem) == Wake::Answered;
    }
    if (p.answered) {
        // One reading varies by a few dB from one transmission to the next:
        // read over a few bursts, and judge the repeater by its level in the
        // heard list, which also counts a current reading from before.
        p.rssiDbm = readLevel();
        noteHeard(i, p.rssiDbm);
        p.levelDb = heardLevelDb(i, p.rssiDbm - floorAt(i));
    }
    logProbe(i, p);
    return p;
}

// Seconds with one decimal, from milliseconds.
lib::StaticString<12> seconds(std::uint32_t ms)
{
    lib::StaticString<12> t;
    t << ms / 1000 << '.' << static_cast<char>('0' + ms / 100 % 10);
    return t;
}

// The heard list, one repeater a line: level and its age.
[[gnu::noinline, gnu::cold]] void logHeardList()
{
    sitesLock.lock();
    roaming::HeardList::Entry entries[roaming::HeardList::Capacity];
    const std::size_t n = std::min(heardList.entries().size(), std::size(entries));
    std::copy_n(heardList.entries().begin(), n, entries);
    sitesLock.unlock();
    const std::uint32_t now = os::nowMs();
    for (std::size_t k = 0; k < n; ++k) {
        const roaming::HeardList::Entry &e = entries[k];
        auto line = log::info("roam");
        line << "heard: repeater " << e.position + 1 << ", " << mhz(siteAt(e.position).receiveHz).view()
             << " MHz, ";
        if (e.samples > 0) {
            line << e.levelDbm << " dBm ";
        } else {
            line << "no level, ";
        }
        line << seconds(now - e.measuredMs).view() << " s ago" << (e.unanswered ? ", unanswered" : "");
    }
}

[[gnu::noinline]] void logSearch(std::size_t count, SearchCause cause)
{
    constexpr const char *Causes[] = {"asked", "for a call"};
    log::info("roam") << "searching the roaming zone of " << count << " (" << Causes[static_cast<int>(cause)] << ")";
}

[[gnu::noinline]] void logSearchEnd(int best, bool complete)
{
    if (best < 0) {
        log::info("roam") << (complete ? "no repeater answered" : "search cut short");
    } else if (best != site) {
        log::info("roam") << "now through repeater " << best + 1;
    } else {
        log::info("roam") << "staying on repeater " << best + 1;
    }
}

[[gnu::noinline]] bool searchZone(SearchCause cause, bool skipCurrent)
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
    logSearch(count, cause);
    leaveCall();
    searching = true;
    away = true;
    showRoaming(true);
    std::uint8_t order[services::CodeplugSource::MaxRoamingSites];
    sitesLock.lock();
    const std::size_t n = roaming::searchOrder(heardList, skipCurrent ? -1 : site, count, os::nowMs(), order);
    sitesLock.unlock();
    roaming::Pick pick(Levels.goodDb);
    std::size_t tried = 0;
    bool done = false;
    while (tried < n && !done && !searchInterrupted()) {
        const int i = order[tried++];
        // The repeater in use gets the wake-ups a call would give it; each
        // of the others one, so that a search keys as few as it can.
        const Probe p = probe(i, i == site ? RepeaterWakeTries : 1);
        if (i == site && p.woken && !p.answered) {
            unanswered();
        }
        done = pick.tried(i, p.answered, p.levelDb);
    }
    const int best = pick.chosen();
    const bool complete = done || tried == n;
    logSearchEnd(best, complete);
    if (best < 0 && complete) {
        ui::post(ui::Notice{"No repeater answered"});
    }
    searching = false;
    if (best >= 0 && best != site) {
        useSite(best);
    } else {
        backToBase();
        showRoaming(false);
    }
    return best >= 0;
}

[[gnu::noinline]] void logMove(int to, roaming::Health state)
{
    constexpr const char *States[] = {"good", "weak", "quiet", "lost"};
    log::info("roam") << "moving from repeater " << site + 1 << " (" << States[static_cast<int>(state)]
                      << ") to repeater " << to + 1;
}

[[gnu::noinline]] void logCheck(std::uint32_t hz, int found, int dbm, bool cut)
{
    lib::StaticString<40> what;
    if (cut) {
        what << "cut short";
    } else if (found < 0) {
        what << "nothing, " << dbm << " dBm";
    } else {
        what << "repeater " << found + 1 << " at " << dbm << " dBm";
    }
    log::info("roam") << "check " << mhz(hz).view() << " MHz: " << what.view();
}

// Tunes to repeater @p i's output for a moment: a carrier, then slot
// timing and a burst. The zone's repeater on that frequency with the
// burst's colour code is heard at the level read. Nothing is transmitted.
// True when a message cut it short.
[[gnu::noinline]] bool check(int i)
{
    away = true;
    tuneTo(i);
    const std::uint32_t hz = tunedHz();
    const Heard h = listenHere(CheckCarrierMs, CheckTimingMs, CheckBurstMs);
    int found = -1;
    if (h.colourCode != NoColourCode) {
        found = findSite([&](const services::RoamingSite &s) {
            return s.receiveHz == hz && colourCodeOf(s) == h.colourCode;
        });
    }
    // Also with nothing found, the level is logged: on an empty channel it
    // shows the noise there.
    if (found >= 0) {
        noteHeard(found, h.rssiDbm);
    }
    logCheck(hz, found, h.rssiDbm, h.cut);
    return h.cut;
}

// A whole pass over the zone, one check after another without going back to
// the repeater in use in between: that keeps the retunes few. Then back.
void scanPass()
{
    std::uint32_t hz[services::CodeplugSource::MaxRoamingSites];
    for (;;) {
        sitesLock.lock();
        if (sitesChanged) {
            // Another zone was chosen: a pass over the old one ends.
            sitesLock.unlock();
            break;
        }
        for (std::size_t k = 0; k < siteCount; ++k) {
            hz[k] = sites[k].receiveHz;
        }
        const int i = listener.next(heardList, std::span<const std::uint32_t>(hz, siteCount), site, os::nowMs());
        const bool last = listener.passDone();
        sitesLock.unlock();
        if (i < 0 || check(i) || last) {
            break;
        }
    }
    backToBase();
}

// Roaming between calls. Nothing moves, and no pass starts, while the
// repeater in use is busy. Moves follow the codeplug's automatic roaming;
// listening runs while the repeater in use is not clearly good: single
// checks during its idle transmissions while it is heard, passes in bursts
// while it is quiet or lost.
void roamStep()
{
    if (!roamingChannel || !canRoam()) {
        return;
    }
    const std::uint32_t now = os::nowMs();
    std::uint32_t hz[services::CodeplugSource::MaxRoamingSites];
    sitesLock.lock();
    if (sitesChanged) {
        sitesLock.unlock();
        return;
    }
    const roaming::Health state = roaming::health(heardList, site, Levels, now);
    const bool busy = activity.busy(now);
    const bool passes = state == roaming::Health::Quiet || state == roaming::Health::Lost;
    const int to = automatic && !busy ? roaming::moveTo(heardList, site, state, Levels, now) : -1;
    const bool listen =
        to < 0 && !(passes && busy) && listener.due(heardList, site, Levels, activity.idle(now), now);
    int next = -1;
    if (listen && !passes) {
        for (std::size_t i = 0; i < siteCount; ++i) {
            hz[i] = sites[i].receiveHz;
        }
        next = listener.next(heardList, std::span<const std::uint32_t>(hz, siteCount), site, now);
    }
    sitesLock.unlock();
    if ((state == roaming::Health::Lost) != shownLost) {
        showRoaming(false);
    }
    if (to >= 0) {
        logMove(to, state);
        useSite(to);
    } else if (listen && passes) {
        scanPass();
        listener.done(os::nowMs());
    } else if (next >= 0) {
        check(next);
        backToBase();
        listener.done(os::nowMs());
    }
}

} // namespace

bool channel()
{
    return roamingChannel;
}

void leave()
{
    roamingChannel = false;
}

int inUse()
{
    return site;
}

bool home()
{
    return !away;
}

void useChannel(const services::Channel &c)
{
    channelReceiveHz = c.receiveHz;
    channelTransmitHz = c.transmitHz;
    channelColourCode = c.dmr.colourCode;
    takeNewZone();
    // A roaming channel keeps the repeater in use; else it starts on its own
    // frequencies when they are one of the zone's repeaters.
    roamingChannel = c.digital() && c.roaming && sitesKnown() > 0;
    if (roamingChannel && site < 0) {
        site = findSite([&](const services::RoamingSite &s) {
            return s.receiveHz == c.receiveHz && s.transmitHz == c.transmitHz;
        });
    }
    sitesLock.lock();
    heardList.keep(site);
    sitesLock.unlock();
    activity.clear();
    if (roamingChannel) {
        tuneTo(site);
    }
}

void show(bool searchShown)
{
    showRoaming(searchShown);
}

void slotTiming()
{
    if (roamingChannel && !away) {
        activity.heard(false, os::nowMs());
    }
}

void callHeard()
{
    if (roamingChannel && !away) {
        activity.callHeard(os::nowMs());
    }
}

void level(int dbm)
{
    if (roamingChannel && !away && site >= 0) {
        noteHeard(site, dbm);
    }
}

void burst(std::uint8_t colourCode, bool call)
{
    if (!roamingChannel || away) {
        return;
    }
    const std::uint32_t now = os::nowMs();
    activity.heard(call, now);
    // Its level is read when its slot timing is found, which stays locked
    // through back-to-back calls: each burst keeps it current meanwhile.
    if (site >= 0 && colourCode == tunedColourCode()) {
        sitesLock.lock();
        heardList.stillHeard(static_cast<std::uint8_t>(site), now);
        sitesLock.unlock();
    }
}

void unanswered()
{
    if (site >= 0) {
        sitesLock.lock();
        heardList.unanswered(static_cast<std::uint8_t>(site), os::nowMs());
        sitesLock.unlock();
    }
}

bool search(SearchCause cause, bool skipCurrent)
{
    return searchZone(cause, skipCurrent);
}

void searchNow()
{
    if (!roamingChannel) {
        log::info("roam") << "not a roaming channel";
        ui::post(ui::Notice{"Not roaming"});
        return;
    }
    searchZone(SearchCause::Asked, false);
}

void use(std::uint8_t position)
{
    if (!roamingChannel || position >= sitesKnown()) {
        log::info("roam") << "not a roaming channel, or no such repeater";
        return;
    }
    useSite(position);
    logStatus();
}

void forget()
{
    sitesLock.lock();
    heardList.clear();
    heardList.keep(site);
    sitesLock.unlock();
    listener.restartPass();
    log::info("roam") << "heard list cleared";
}

void setAutomatic(bool on)
{
    automatic = on;
    log::info("roam") << "automatic " << (on ? "on" : "off");
}

void setup(const services::RoamingSettings &settings)
{
    automatic = settings.automatic;
    takeNewZone();
    log::info("roam") << sitesKnown() << " repeaters in the roaming zone; automatic " << (automatic ? "on" : "off");
}

void logStatus()
{
    log::info("roam") << (roamingChannel ? "roaming channel" : "not a roaming channel")
                      << ", " << sitesKnown() << " repeaters, in use: " << site + 1 << " ("
                      << mhz(tunedHz()).view() << " MHz, colour code " << tunedColourCode()
                      << "), automatic " << (automatic ? "on" : "off");
    logHeardList();
}

void step()
{
    roamStep();
}

} // namespace dmm::app::radio::roam

namespace dmm::app::radio {

using namespace roam;

void roamingView(RoamingView &out)
{
    sitesLock.lock();
    out.roaming = roamingChannel;
    out.searching = searching;
    out.site = site;
    out.health = services::roaming::health(heardList, site, Levels, os::nowMs());
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
    newSiteCount = std::min(fill(newSites), std::size(newSites));
    sitesChanged = true;
    sitesLock.unlock();
}

} // namespace dmm::app::radio
