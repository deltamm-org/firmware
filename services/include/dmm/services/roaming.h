// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/codeplug.h"

#include <cstddef>
#include <cstdint>
#include <span>

/// DMR roaming: the repeaters heard, the order to search the roaming zone
/// in, which repeater a search settles on, and when to search by itself.
/// Positions are places in the roaming zone, 0 to MaxRoamingSites - 1.
/// Times are milliseconds on a clock that may wrap.
namespace dmm::services::roaming {

/// The repeaters of the roaming zone heard lately, a few at a time: their
/// level and, once learned, when they send their beacons.
class HeardList
{
public:
    static constexpr std::size_t Capacity = 8;
    /// A level older than this is unknown; after a beacon, one older than
    /// this and the longest beacon, and with the beacon period known, one
    /// older than a period and the longest beacon. A beacon's level so
    /// stays fresh until the next beacon of the usual period has come.
    static constexpr std::uint32_t ValidMs = 60'000;
    /// Repeaters send beacons every 10 s to 210 s, each 0.72 s to 16.32 s
    /// long, as each is set up. Periods outside this range are not taken
    /// as periods, and longer transmissions are not beacons.
    static constexpr std::uint32_t MinPeriodMs = 10'000;
    static constexpr std::uint32_t MaxPeriodMs = 600'000;
    static constexpr std::uint32_t MaxBeaconMs = 16'500;
    /// Beacons expected and not heard, in a row, before the timing is
    /// forgotten.
    static constexpr std::uint8_t MaxMisses = 2;

    struct Entry
    {
        std::uint8_t position = 0;
        std::int8_t levelDbm = 0;
        std::uint8_t misses = 0;
        std::uint8_t samples = 0;   // levels measured while fresh, up to 255
        std::uint16_t periodCs = 0; // beacon period in 10 ms steps; 0 = not known
        std::uint16_t lengthCs = 0; // the last beacon's length in 10 ms steps; 0 = not known
        std::uint32_t beaconMs = 0; // when the last beacon started; 0 = none heard
        std::uint32_t measuredMs = 0;

        std::uint32_t periodMs() const { return periodCs * 10u; }
        std::uint32_t lengthMs() const { return lengthCs * 10u; }
        std::uint32_t validMs() const
        {
            if (periodCs == 0) {
                return beaconMs != 0 ? ValidMs + MaxBeaconMs : ValidMs;
            }
            return periodMs() + MaxBeaconMs > ValidMs ? periodMs() + MaxBeaconMs : ValidMs;
        }
        bool fresh(std::uint32_t nowMs) const { return nowMs - measuredMs < validMs(); }
    };

    void clear() { m_count = 0; }
    /// Repeater @p position heard at @p levelDbm.
    void heard(std::uint8_t position, int levelDbm, std::uint32_t nowMs);
    /// A beacon of repeater @p position started at @p nowMs, heard at
    /// @p levelDbm, and lasted @p lengthMs (0 = not known). Two beacons give
    /// the period; later ones refine it, also across missed ones. One that
    /// comes late moves the beat; one that comes early is taken only when
    /// it divides the period, which then spanned missed beacons.
    void beacon(std::uint8_t position, int levelDbm, std::uint32_t nowMs, std::uint32_t lengthMs = 0);
    /// A beacon of repeater @p position did not come when it was due: the
    /// next one is expected a period later, until MaxMisses in a row make
    /// the beat unknown.
    void missed(std::uint8_t position);
    /// When the next beacon of @p e is due after @p nowMs; 0 while its
    /// timing is not known.
    static std::uint32_t nextBeacon(const Entry &e, std::uint32_t nowMs);

    const Entry *find(std::uint8_t position) const;
    std::span<const Entry> entries() const { return {m_entries, m_count}; }

private:
    Entry &place(std::uint8_t position, std::uint32_t nowMs);

    Entry m_entries[Capacity];
    std::size_t m_count = 0;
};

/// A search order: positions to try, and where the rest of the zone begins
/// in it.
struct Order
{
    std::size_t count = 0;
    std::size_t restBegins = 0;
};

/// The order to try the zone's @p zoneSize repeaters in, most likely first:
/// @p current (when >= 0), then the heard list, fresh levels before stale
/// ones and strongest first, then the rest of the zone in its own order
/// from position @p from, wrapping. Each position comes once.
Order searchOrder(const HeardList &heard, int current, std::size_t zoneSize, std::size_t from, std::uint32_t nowMs,
    std::span<std::uint8_t> out);

/// Which repeater a search settles on, fed with each repeater it tried.
/// A search stops at the first repeater at or above @p goodDbm; otherwise
/// it takes the strongest that answered. The repeater in use (@p current)
/// stays unless the other is at least @p marginDb stronger.
class Pick
{
public:
    Pick(int current, int goodDbm, int marginDb);
    /// Repeater @p position answered at @p levelDbm, or did not answer.
    /// True when the search can stop.
    bool tried(int position, bool answered, int levelDbm);
    /// The repeater to use; -1 when none answered.
    int chosen() const;

private:
    int m_current;
    int m_goodDbm;
    int m_marginDb;
    int m_best = -1;
    int m_bestDbm = 0;
    bool m_currentAnswered = false;
    int m_currentDbm = 0;
};

/// Signal levels that steer roaming. Levels below about -100 dBm cannot be
/// told from no signal on the D878UVII's receiver, so the thresholds sit
/// above that.
struct Levels
{
    int goodDbm = -90;     // a search stops here
    int weakDbm = -95;     // below: look for a better repeater
    int failingDbm = -105; // below: take any better repeater
    int slowMarginDb = 6;  // better by this to move while weak
    int fastMarginDb = 3;  // better by this to move while failing
};

/// How the repeater in use is doing.
enum class Health : std::uint8_t
{
    Unknown, // not heard lately, and no beacon timing to miss
    Good,
    Weak,
    Failing,
    Lost, // its beacons stopped and nothing else was heard
};

/// The health of repeater @p site (-1: none in use, which counts as lost).
Health health(const HeardList &heard, int site, const Levels &levels, std::uint32_t nowMs);

/// The repeater to move to from @p site in state @p state, or -1 to stay:
/// the strongest fresh one, measured at least twice (once when lost),
/// better by the margin the state asks.
int moveTo(const HeardList &heard, int site, Health state, const Levels &levels, std::uint32_t nowMs);

/// A visit to another repeater: tune to it, listen, come back.
struct Visit
{
    int position = -1;          // -1: no visit now
    std::uint32_t dwellMs = 0;  // the longest to stay
    std::uint32_t beaconMs = 0; // the beacon expected, for a timed visit
    bool sweep = false;         // looking for a beacon whose timing is not known
};

/// Plans visits while the repeater in use is quiet. A repeater whose beacon
/// timing is known is visited just before its next beacon: every
/// RefreshMs while the one in use is good, at every beacon otherwise. The
/// rest are swept, one visit at a time in zone order, each visit landing
/// at a different point of the beacon period, until a beacon is caught.
class Visits
{
public:
    static constexpr std::uint32_t LeadMs = 300;
    static constexpr std::uint32_t AfterMs = 500;
    static constexpr std::uint32_t RefreshMs = 300'000;
    static constexpr std::uint32_t SweepDwellMs = 1000;
    /// A beacon under way when a sweep arrived: the next visit comes this
    /// much later, about one 60 s period less a beacon, and stays long
    /// enough to see the next one start.
    static constexpr std::uint32_t FollowUpAfterMs = 56'000;
    static constexpr std::uint32_t FollowUpDwellMs = 4500;

    /// Time between sweep visits in state @p state: more than a 60 s
    /// period, so that each lands later in the period, when all is well.
    static std::uint32_t sweepGapMs(Health state);

    /// The visit due now, if any.
    Visit due(const HeardList &heard, int site, std::size_t zoneSize, Health state, std::uint32_t nowMs);

    enum class Found : std::uint8_t
    {
        Nothing,
        Start,    // a beacon started during the visit
        UnderWay, // a transmission was on when the visit began
    };
    /// A sweep visit to @p position, which began at @p arrivedMs, ended.
    void swept(int position, Found found, std::uint32_t arrivedMs, Health state);

private:
    std::size_t m_cursor = 0;
    std::uint32_t m_nextSweepMs = 0;
    bool m_sweepWaits = false;
    int m_followUp = -1;
    std::uint32_t m_followUpMs = 0;
};

/// The codeplug's repeater range check: while the repeater in use is not
/// heard, a wake-up every interval; enough unanswered ones in a row put it
/// out of range, and the user is told as many times as the settings ask,
/// once an interval.
class RangeCheck
{
public:
    void configure(const RoamingSettings &settings, std::uint32_t nowMs);
    /// The repeater in use was heard: it is in range.
    void heard(std::uint32_t nowMs);
    /// A check is due now.
    bool due(std::uint32_t nowMs) const;
    /// A check ended. True when this one put the repeater out of range.
    bool checked(bool answered, std::uint32_t nowMs);
    bool outOfRange() const { return m_failures >= m_settings.checkTries && m_settings.rangeCheck; }
    /// The user is to be told now that the repeater is out of range.
    bool noticeDue(std::uint32_t nowMs);

private:
    RoamingSettings m_settings;
    std::uint32_t m_heardMs = 0;
    std::uint32_t m_checkedMs = 0;
    std::uint32_t m_noticeMs = 0;
    std::uint8_t m_failures = 0;
    std::uint8_t m_notices = 0;
};

/// When the radio searches by itself, as RoamingSettings ask.
class Watch
{
public:
    /// Shortest quiet time and period, whatever the settings say: each
    /// search wakes repeaters.
    static constexpr std::uint32_t MinQuietMs = 30'000;
    static constexpr std::uint32_t MinPeriodMs = 60'000;

    void configure(const RoamingSettings &settings, std::uint32_t nowMs);
    bool automatic() const { return m_settings.automatic; }
    void setAutomatic(bool on) { m_settings.automatic = on; }
    /// The repeater in use was heard.
    void heard(std::uint32_t nowMs) { m_heard = nowMs; }
    /// A search ended.
    void searched(std::uint32_t nowMs) { m_searched = nowMs; }
    /// A search is due now.
    bool due(std::uint32_t nowMs) const;

private:
    RoamingSettings m_settings;
    std::uint32_t m_heard = 0;
    std::uint32_t m_searched = 0;
};

} // namespace dmm::services::roaming
