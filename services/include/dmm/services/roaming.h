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
    /// A level older than this is unknown.
    static constexpr std::uint32_t ValidMs = 60'000;
    /// Beacon periods outside this range are not taken as periods.
    static constexpr std::uint32_t MinPeriodMs = 10'000;
    static constexpr std::uint32_t MaxPeriodMs = 600'000;
    /// Beacons expected and not heard, in a row, before the timing is
    /// forgotten.
    static constexpr std::uint8_t MaxMisses = 2;

    struct Entry
    {
        std::uint8_t position = 0;
        std::int8_t levelDbm = 0;
        std::uint8_t misses = 0;
        std::uint8_t sweep = 0;     // the part of the beacon period to look at next
        std::uint16_t periodCs = 0; // beacon period in 10 ms steps; 0 = not known
        std::uint32_t beaconMs = 0; // when the last beacon started; 0 = none heard
        std::uint32_t measuredMs = 0;

        std::uint32_t periodMs() const { return periodCs * 10u; }
        bool fresh(std::uint32_t nowMs) const { return nowMs - measuredMs < ValidMs; }
    };

    void clear() { m_count = 0; }
    /// Repeater @p position heard at @p levelDbm.
    void heard(std::uint8_t position, int levelDbm, std::uint32_t nowMs);
    /// A beacon of repeater @p position started at @p nowMs, heard at
    /// @p levelDbm. Two beacons give the period; later ones refine it, also
    /// across missed ones, and one off the period's beat moves the beat.
    void beacon(std::uint8_t position, int levelDbm, std::uint32_t nowMs);
    /// A beacon of repeater @p position did not come when it was due.
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
