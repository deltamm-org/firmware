// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/roaming.h"

#include <algorithm>

namespace dmm::services::roaming {

namespace {

std::int8_t clampDbm(int dbm)
{
    return static_cast<std::int8_t>(std::clamp(dbm, -128, 127));
}

// Takes a beacon starting at @p nowMs into the beat of @p e.
void keepBeat(HeardList::Entry &e, std::uint32_t nowMs)
{
    using L = HeardList;
    if (e.beaconMs == 0) {
        e.beaconMs = nowMs;
        e.misses = 0;
        return;
    }
    if (static_cast<std::int32_t>(nowMs - e.beaconMs) < 0) {
        // Moved ahead by a missed beacon, and came late after all: the beat
        // follows it.
        e.beaconMs = nowMs;
        e.misses = 0;
        return;
    }
    const std::uint32_t since = nowMs - e.beaconMs;
    if (since < L::MinPeriodMs) {
        // Still the same beacon, or traffic: the beat stays.
        return;
    }
    if (e.periodCs == 0) {
        if (since <= L::MaxPeriodMs) {
            e.periodCs = static_cast<std::uint16_t>(since / 10);
        }
    } else {
        const std::uint32_t period = e.periodMs();
        const std::uint32_t beats = (since + period / 2) / period;
        const std::uint32_t off = since > beats * period ? since - beats * period : beats * period - since;
        const std::uint32_t tolerance = std::max<std::uint32_t>(300, period / 100);
        if (beats >= 1 && off <= tolerance) {
            const std::uint32_t measured = since / beats;
            e.periodCs = static_cast<std::uint16_t>((period * 3 + measured) / 4 / 10);
        } else if (since < period) {
            // Early: a shorter period that divides the learned one, which
            // then spanned missed beacons; anything else is not a beacon.
            const std::uint32_t parts = (period + since / 2) / since;
            const std::uint32_t rest = period > parts * since ? period - parts * since : parts * since - period;
            if (parts < 2 || rest > tolerance * parts) {
                return;
            }
            e.periodCs = static_cast<std::uint16_t>(since / 10);
        }
        // Otherwise the beacon came late, after the repeater was busy: the
        // period stays and the beat moves to this beacon.
    }
    e.beaconMs = nowMs;
    e.misses = 0;
}

} // namespace

HeardList::Entry &HeardList::place(std::uint8_t position, std::uint32_t nowMs)
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].position == position) {
            return m_entries[i];
        }
    }
    std::size_t i = m_count;
    if (m_count < Capacity) {
        ++m_count;
    } else {
        // The one measured longest ago gives way.
        i = 0;
        for (std::size_t j = 1; j < m_count; ++j) {
            if (nowMs - m_entries[j].measuredMs > nowMs - m_entries[i].measuredMs) {
                i = j;
            }
        }
    }
    m_entries[i] = Entry{};
    m_entries[i].position = position;
    return m_entries[i];
}

void HeardList::heard(std::uint8_t position, int levelDbm, std::uint32_t nowMs)
{
    const bool known = find(position) != nullptr;
    Entry &e = place(position, nowMs);
    // A fresh level is smoothed; a stale or new one is replaced.
    const bool fresh = known && e.fresh(nowMs);
    e.levelDbm = clampDbm(fresh ? (e.levelDbm + levelDbm) / 2 : levelDbm);
    e.samples = fresh ? static_cast<std::uint8_t>(std::min(e.samples + 1, 255)) : 1;
    e.measuredMs = nowMs;
}

void HeardList::beacon(std::uint8_t position, int levelDbm, std::uint32_t nowMs, std::uint32_t lengthMs)
{
    // The beat first, so that a period learned from this beacon already
    // keeps the last level fresh.
    const bool known = find(position) != nullptr;
    if (known) {
        keepBeat(place(position, nowMs), nowMs);
    }
    heard(position, levelDbm, nowMs);
    Entry &e = place(position, nowMs);
    if (!known) {
        keepBeat(e, nowMs);
    }
    if (lengthMs != 0 && lengthMs <= MaxBeaconMs) {
        e.lengthCs = static_cast<std::uint16_t>(lengthMs / 10);
    }
}

void HeardList::missed(std::uint8_t position)
{
    for (std::size_t i = 0; i < m_count; ++i) {
        Entry &e = m_entries[i];
        if (e.position != position || e.beaconMs == 0 || e.periodCs == 0) {
            continue;
        }
        if (++e.misses >= MaxMisses) {
            // The period still holds; when the beacon comes is unknown.
            e.misses = 0;
            e.beaconMs = 0;
        } else {
            e.beaconMs += e.periodMs();
        }
    }
}

std::uint32_t HeardList::nextBeacon(const Entry &e, std::uint32_t nowMs)
{
    if (e.periodCs == 0 || e.beaconMs == 0) {
        return 0;
    }
    if (static_cast<std::int32_t>(e.beaconMs - nowMs) > 0) {
        return e.beaconMs; // moved ahead by a missed beacon
    }
    const std::uint32_t period = e.periodMs();
    const std::uint32_t next = e.beaconMs + ((nowMs - e.beaconMs) / period + 1) * period;
    return next != 0 ? next : 1;
}

const HeardList::Entry *HeardList::find(std::uint8_t position) const
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].position == position) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

Order searchOrder(const HeardList &heard, int current, std::size_t zoneSize, std::size_t from, std::uint32_t nowMs,
    std::span<std::uint8_t> out)
{
    Order o;
    std::uint64_t used = 0;
    auto add = [&](std::size_t position) {
        const std::uint64_t bit = std::uint64_t{1} << position;
        if (position < zoneSize && position < 64 && o.count < out.size() && (used & bit) == 0) {
            used |= bit;
            out[o.count++] = static_cast<std::uint8_t>(position);
        }
    };
    if (current >= 0) {
        add(static_cast<std::size_t>(current));
    }
    const std::span<const HeardList::Entry> entries = heard.entries();
    const HeardList::Entry *sorted[HeardList::Capacity];
    std::size_t n = 0;
    for (const HeardList::Entry &e : entries) {
        sorted[n++] = &e;
    }
    std::sort(sorted, sorted + n, [nowMs](const HeardList::Entry *a, const HeardList::Entry *b) {
        if (a->fresh(nowMs) != b->fresh(nowMs)) {
            return a->fresh(nowMs);
        }
        return a->levelDbm > b->levelDbm;
    });
    for (std::size_t i = 0; i < n; ++i) {
        add(sorted[i]->position);
    }
    o.restBegins = o.count;
    for (std::size_t i = 0; zoneSize > 0 && i < zoneSize; ++i) {
        add((from + i) % zoneSize);
    }
    return o;
}

Pick::Pick(int current, int goodDbm, int marginDb) : m_current(current),
                                                     m_goodDbm(goodDbm),
                                                     m_marginDb(marginDb)
{
}

bool Pick::tried(int position, bool answered, int levelDbm)
{
    if (position == m_current) {
        m_currentAnswered = answered;
        m_currentDbm = levelDbm;
        return answered && levelDbm >= m_goodDbm;
    }
    if (!answered) {
        return false;
    }
    if (m_best < 0 || levelDbm > m_bestDbm) {
        m_best = position;
        m_bestDbm = levelDbm;
    }
    return levelDbm >= m_goodDbm && (!m_currentAnswered || levelDbm - m_currentDbm >= m_marginDb);
}

int Pick::chosen() const
{
    if (m_best < 0) {
        return m_currentAnswered ? m_current : -1;
    }
    if (!m_currentAnswered) {
        return m_best;
    }
    const int gain = m_bestDbm - m_currentDbm;
    return gain > 0 && gain >= m_marginDb ? m_best : m_current;
}

Health health(const HeardList &heard, int site, const Levels &levels, std::uint32_t nowMs)
{
    if (site < 0) {
        return Health::Lost;
    }
    const HeardList::Entry *e = heard.find(static_cast<std::uint8_t>(site));
    if (e == nullptr) {
        return Health::Unknown;
    }
    // Its beacon timing was forgotten after missed beacons, and nothing
    // was heard for two periods.
    if (e->periodCs != 0 && e->beaconMs == 0 && nowMs - e->measuredMs > 2 * e->periodMs()) {
        return Health::Lost;
    }
    // While its beacons keep their beat, the last level holds until one is
    // missed, however long the period.
    const bool timed = e->periodCs != 0 && e->beaconMs != 0;
    if (!timed && !e->fresh(nowMs)) {
        return Health::Unknown;
    }
    return e->levelDbm >= levels.weakDbm   ? Health::Good
        : e->levelDbm >= levels.failingDbm ? Health::Weak
                                           : Health::Failing;
}

int moveTo(const HeardList &heard, int site, Health state, const Levels &levels, std::uint32_t nowMs)
{
    if (state != Health::Weak && state != Health::Failing && state != Health::Lost) {
        return -1;
    }
    const HeardList::Entry *own = site >= 0 ? heard.find(static_cast<std::uint8_t>(site)) : nullptr;
    const int ownDbm = own != nullptr && state != Health::Lost ? own->levelDbm : -1000;
    const int margin = state == Health::Weak ? levels.slowMarginDb : state == Health::Failing ? levels.fastMarginDb
                                                                                              : 0;
    const std::uint8_t samples = state == Health::Lost ? 1 : 2;
    const HeardList::Entry *best = nullptr;
    for (const HeardList::Entry &e : heard.entries()) {
        if (static_cast<int>(e.position) != site && e.fresh(nowMs) && e.samples >= samples &&
            (best == nullptr || e.levelDbm > best->levelDbm)) {
            best = &e;
        }
    }
    return best != nullptr && best->levelDbm >= ownDbm + margin ? best->position : -1;
}

std::uint32_t Visits::sweepGapMs(Health state)
{
    switch (state) {
    case Health::Good:
    case Health::Unknown:
        return 63'500;
    case Health::Weak:
    case Health::Failing:
        return 16'000;
    case Health::Lost:
        break;
    }
    return 2000;
}

Visit Visits::due(const HeardList &heard, int site, std::size_t zoneSize, Health state, std::uint32_t nowMs)
{
    // A beacon of a repeater with known timing, about to start.
    const std::uint32_t refresh = state == Health::Good ? RefreshMs : 10'000;
    for (const HeardList::Entry &e : heard.entries()) {
        if (static_cast<int>(e.position) == site || e.position >= zoneSize || nowMs - e.measuredMs < refresh) {
            continue;
        }
        const std::uint32_t next = HeardList::nextBeacon(e, nowMs);
        if (next != 0 && next - nowMs <= LeadMs) {
            return {e.position, next - nowMs + AfterMs, next, false};
        }
    }
    if (m_followUp >= 0 && static_cast<std::int32_t>(nowMs - m_followUpMs) >= 0) {
        return {m_followUp, FollowUpDwellMs, 0, true};
    }
    if (m_sweepWaits && static_cast<std::int32_t>(nowMs - m_nextSweepMs) < 0) {
        return {};
    }
    // The next repeater of the zone whose beacon timing is not known.
    for (std::size_t i = 0; i < zoneSize; ++i) {
        const std::size_t p = (m_cursor + i) % zoneSize;
        const HeardList::Entry *e = heard.find(static_cast<std::uint8_t>(p));
        const bool timed = e != nullptr && e->periodCs != 0 && e->beaconMs != 0;
        if (static_cast<int>(p) != site && !timed) {
            m_cursor = p;
            return {static_cast<int>(p), SweepDwellMs, 0, true};
        }
    }
    return {};
}

void Visits::swept(int position, Found found, std::uint32_t arrivedMs, Health state)
{
    m_sweepWaits = true;
    m_nextSweepMs = arrivedMs + sweepGapMs(state);
    if (found == Found::UnderWay && m_followUp != position) {
        m_followUp = position;
        m_followUpMs = arrivedMs + FollowUpAfterMs;
        return;
    }
    if (position == m_followUp) {
        m_followUp = -1;
    }
    m_cursor = static_cast<std::size_t>(position) + 1;
}

void RangeCheck::configure(const RoamingSettings &settings, std::uint32_t nowMs)
{
    m_settings = settings;
    heard(nowMs);
}

void RangeCheck::heard(std::uint32_t nowMs)
{
    m_heardMs = nowMs;
    m_checkedMs = nowMs;
    m_failures = 0;
    m_notices = 0;
}

bool RangeCheck::due(std::uint32_t nowMs) const
{
    const std::uint32_t interval = m_settings.checkIntervalS * 1000u;
    return m_settings.rangeCheck && nowMs - m_heardMs >= interval && nowMs - m_checkedMs >= interval;
}

bool RangeCheck::checked(bool answered, std::uint32_t nowMs)
{
    if (answered) {
        heard(nowMs);
        return false;
    }
    m_checkedMs = nowMs;
    if (m_failures < 255) {
        ++m_failures;
    }
    return m_settings.rangeCheck && m_failures == m_settings.checkTries;
}

bool RangeCheck::noticeDue(std::uint32_t nowMs)
{
    if (!outOfRange() || m_notices >= m_settings.outOfRangeNotices ||
        (m_notices > 0 && nowMs - m_noticeMs < m_settings.checkIntervalS * 1000u)) {
        return false;
    }
    ++m_notices;
    m_noticeMs = nowMs;
    return true;
}

void Watch::configure(const RoamingSettings &settings, std::uint32_t nowMs)
{
    m_settings = settings;
    m_heard = nowMs;
    m_searched = nowMs;
}

bool Watch::due(std::uint32_t nowMs) const
{
    if (!m_settings.automatic) {
        return false;
    }
    if (m_settings.start == RoamingSettings::Start::Periodic) {
        const std::uint32_t period = std::max<std::uint32_t>(m_settings.periodMinutes * 60'000u, MinPeriodMs);
        return nowMs - m_searched >= period;
    }
    const std::uint32_t quiet = std::max<std::uint32_t>(m_settings.quietS * 1000u, MinQuietMs);
    return nowMs - m_heard >= quiet && nowMs - m_searched >= quiet;
}

} // namespace dmm::services::roaming
