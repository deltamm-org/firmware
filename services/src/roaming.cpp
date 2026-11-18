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

// Sorts the few @p entries by @p before, keeping the order of equals.
template <class Before>
void sortEntries(const HeardList::Entry **entries, std::size_t n, Before before)
{
    for (std::size_t a = 1; a < n; ++a) {
        for (std::size_t b = a; b > 0 && before(entries[b], entries[b - 1]); --b) {
            std::swap(entries[b - 1], entries[b]);
        }
    }
}

bool stronger(const HeardList::Entry *a, const HeardList::Entry *b)
{
    return a->levelDb() > b->levelDb();
}

// The current entries other than @p site, strongest first, into @p out.
std::size_t strongestFirst(const HeardList &heard, int site, std::uint32_t nowMs, const HeardList::Entry **out)
{
    std::size_t n = 0;
    for (const HeardList::Entry &e : heard.entries()) {
        if (static_cast<int>(e.position) != site && e.current(nowMs)) {
            out[n++] = &e;
        }
    }
    sortEntries(out, n, stronger);
    return n;
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
        // The one measured longest ago gives way, never the one in use.
        i = Capacity;
        for (std::size_t j = 0; j < m_count; ++j) {
            if (static_cast<int>(m_entries[j].position) == m_keep) {
                continue;
            }
            if (i == Capacity || nowMs - m_entries[j].measuredMs > nowMs - m_entries[i].measuredMs) {
                i = j;
            }
        }
    }
    m_entries[i] = Entry{};
    m_entries[i].position = position;
    m_entries[i].measuredMs = nowMs;
    return m_entries[i];
}

void HeardList::keep(int position)
{
    m_keep = position;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].samples > 0 || static_cast<int>(m_entries[i].position) == position) {
            m_entries[kept++] = m_entries[i];
        }
    }
    m_count = kept;
}

void HeardList::heard(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, int fairDb)
{
    Entry &e = place(position, nowMs);
    // A reading moves a current level part of the way; an old one is replaced.
    const bool current = e.current(nowMs);
    const std::int32_t to = 16 * clampDbm(levelDbm);
    if (current) {
        const auto dt = static_cast<std::int32_t>(std::min(nowMs - e.readMs, ValidMs));
        const std::int32_t way = (to - e.level16Dbm) * dt;
        const std::int32_t whole = dt + static_cast<std::int32_t>(CombineMs);
        e.level16Dbm = static_cast<std::int16_t>(e.level16Dbm + (way + (way < 0 ? -whole : whole) / 2) / whole);
    } else {
        e.level16Dbm = static_cast<std::int16_t>(to);
    }
    e.floorDbm = clampDbm(floorDbm);
    if (!current || e.levelDb() >= fairDb) {
        e.aboveFairMs = nowMs;
    }
    e.unanswered = false;
    e.samples = current ? static_cast<std::uint8_t>(std::min(e.samples + 1, 255)) : 1;
    e.measuredMs = nowMs;
    e.readMs = nowMs;
}

void HeardList::take()
{
    const Pending p = m_pending;
    m_pending = {};
    if (p.position < 0 || p.bursts == 0) {
        return;
    }
    const Entry *e = find(static_cast<std::uint8_t>(p.position));
    if (p.bursts < ReadingBursts && e != nullptr && e->current(p.lastMs)) {
        return;
    }
    // The mean, rounded to the nearest dB.
    const int mean = static_cast<int>((2 * p.sumDbm + (p.sumDbm < 0 ? -p.bursts : p.bursts)) / (2 * p.bursts));
    heard(static_cast<std::uint8_t>(p.position), mean, p.floorDbm, p.lastMs, p.fairDb);
}

void HeardList::started(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, int fairDb)
{
    take();
    m_pending = {position, floorDbm, fairDb, clampDbm(levelDbm), 1, nowMs, nowMs};
}

void HeardList::stillHeard(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, int fairDb)
{
    Entry *e = nullptr;
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_entries[i].position == position && m_entries[i].current(nowMs)) {
            e = &m_entries[i];
            e->measuredMs = nowMs;
        }
    }
    if (m_pending.position == position) {
        m_pending.sumDbm += clampDbm(levelDbm);
        ++m_pending.bursts;
        m_pending.lastMs = nowMs;
        if (nowMs - m_pending.fromMs >= ReadingSpanMs) {
            take();
        }
    } else if (e == nullptr || nowMs - e->readMs >= ReadingMs) {
        started(position, levelDbm, floorDbm, nowMs, fairDb);
    }
}

void HeardList::flush(std::uint32_t nowMs)
{
    if (m_pending.position >= 0 && (nowMs - m_pending.lastMs >= ReadingGapMs || nowMs - m_pending.fromMs >= ReadingSpanMs)) {
        take();
    }
}

void HeardList::unanswered(std::uint8_t position, std::uint32_t nowMs)
{
    place(position, nowMs).unanswered = true;
}

void HeardList::renumber(std::span<const std::int16_t> moved)
{
    auto to = [&](int position) {
        return position >= 0 && static_cast<std::size_t>(position) < moved.size() ? moved[position] : -1;
    };
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_count; ++i) {
        const int position = to(m_entries[i].position);
        if (position >= 0) {
            m_entries[kept] = m_entries[i];
            m_entries[kept++].position = static_cast<std::uint8_t>(position);
        }
    }
    m_count = kept;
    m_keep = to(m_keep);
    m_pending = {}; // a reading of the old zone
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

std::size_t searchOrder(const HeardList &heard, int current, std::size_t zoneSize, std::uint32_t nowMs,
    std::span<std::uint8_t> out)
{
    std::size_t count = 0;
    std::uint64_t used = 0;
    auto add = [&](std::size_t position) {
        const std::uint64_t bit = std::uint64_t{1} << position;
        if (position < zoneSize && position < 64 && count < out.size() && (used & bit) == 0) {
            used |= bit;
            out[count++] = static_cast<std::uint8_t>(position);
        }
    };
    if (current >= 0) {
        add(static_cast<std::size_t>(current));
    }
    // Current levels strongest first, then old ones strongest first.
    const HeardList::Entry *sorted[HeardList::Capacity];
    std::size_t n = 0;
    for (const HeardList::Entry &e : heard.entries()) {
        sorted[n++] = &e;
    }
    sortEntries(sorted, n, [nowMs](const HeardList::Entry *a, const HeardList::Entry *b) {
        if (a->current(nowMs) != b->current(nowMs)) {
            return a->current(nowMs);
        }
        return stronger(a, b);
    });
    for (std::size_t i = 0; i < n; ++i) {
        if (sorted[i]->samples > 0) {
            add(sorted[i]->position);
        }
    }
    for (std::size_t i = 0; i < zoneSize; ++i) {
        add(i);
    }
    return count;
}

bool Pick::tried(int position, bool answered, int levelDb)
{
    if (!answered) {
        return false;
    }
    if (m_best < 0 || levelDb > m_bestDb) {
        m_best = position;
        m_bestDb = levelDb;
    }
    return levelDb >= m_goodDb;
}

Health health(const HeardList &heard, int site, const Levels &levels, std::uint32_t nowMs)
{
    const HeardList::Entry *e = site >= 0 ? heard.find(static_cast<std::uint8_t>(site)) : nullptr;
    if (e == nullptr) {
        return Health::Quiet;
    }
    if (!e->current(nowMs)) {
        // Silent after being heard well: likely just idle. Silent after
        // fading below the fair level: likely out of reach.
        const bool faded = e->samples > 0 && e->levelDb() < levels.fairDb;
        return e->unanswered || faded ? Health::Lost : Health::Quiet;
    }
    if (e->unanswered) {
        return Health::Fair;
    }
    if (e->levelDb() < levels.fairDb) {
        // Heard, but only below the fair level for as long as a level
        // stays current: close to the edge of decoding.
        return nowMs - e->aboveFairMs >= HeardList::ValidMs ? Health::Poor : Health::Fair;
    }
    return e->levelDb() >= levels.strongDb ? Health::Strong : Health::Good;
}

int moveTo(const HeardList &heard, int site, Health state, const Levels &levels, std::uint32_t nowMs)
{
    if (state == Health::Strong) {
        return -1;
    }
    const HeardList::Entry *sorted[HeardList::Capacity];
    const std::size_t n = strongestFirst(heard, site, nowMs, sorted);
    for (std::size_t i = 0; i < n; ++i) {
        const HeardList::Entry &e = *sorted[i];
        switch (state) {
        case Health::Good:
        case Health::Fair: {
            const HeardList::Entry *own = heard.find(static_cast<std::uint8_t>(site));
            if (e.samples >= 2) {
                return e.levelDb() >= own->levelDb() + levels.marginDb ? e.position : -1;
            }
            break; // measured once: the next strongest may do
        }
        case Health::Poor: {
            const HeardList::Entry *own = heard.find(static_cast<std::uint8_t>(site));
            return e.levelDb() >= own->levelDb() + levels.marginDb ? e.position : -1;
        }
        case Health::Quiet:
            return e.levelDb() >= levels.searchDb ? e.position : -1;
        default: {
            // Faded out: only to one stronger than it was last heard, so
            // that a weak repeater gone quiet is not left for a weaker one.
            const HeardList::Entry *own = site >= 0 ? heard.find(static_cast<std::uint8_t>(site)) : nullptr;
            const bool faded = own != nullptr && !own->unanswered && own->samples > 0;
            return !faded || e.levelDb() > own->levelDb() ? e.position : -1;
        }
        }
    }
    return -1;
}

void Activity::heard(bool call, std::uint32_t nowMs)
{
    if (!onAir(nowMs)) {
        m_startMs = nowMs;
    }
    m_heard = true;
    m_lastMs = nowMs;
    if (call) {
        m_call = true;
        m_callMs = nowMs;
    }
}

bool Listening::due(const HeardList &heard, int site, const Levels &levels, bool idle, std::uint32_t nowMs,
    bool busy)
{
    const Health state = health(heard, site, levels, nowMs);
    if (state == Health::Strong || state == Health::Good || state == Health::Fair) {
        m_scanning = false;
        m_planned = state != Health::Strong;
        if (state == Health::Strong) {
            m_scanBurst = false;
            return false;
        }
        // A burst of passes ScanGoodMs after the last one while good,
        // ScanFairMs while fair; the first after it was first below
        // strong. A move ends a burst.
        if (!m_scanTimer) {
            m_scanTimer = true;
            m_scanLastMs = nowMs;
        }
        if (site != m_scanSite) {
            m_scanSite = site;
            m_scanBurst = false;
        }
        if (m_scanBurst && static_cast<std::int32_t>(nowMs - m_scanEndMs) >= 0) {
            m_scanBurst = false;
        }
        const std::uint32_t period = state == Health::Good ? ScanGoodMs : ScanFairMs;
        m_scanPeriodMs = period;
        if (!m_scanBurst && !busy && nowMs - m_scanLastMs >= period) {
            m_scanBurst = true;
            m_scanEndMs = nowMs + BurstMs;
            m_scanLastMs = nowMs;
        }
        if (m_scanBurst) {
            return !busy && (!m_any || nowMs - m_lastMs >= DwellMs);
        }
        return idle && (!m_any || nowMs - m_lastMs >= HopMs);
    }
    m_scanBurst = false;
    m_planned = true;
    if (!m_scanning) {
        m_scanning = true;
        m_burstEndMs = nowMs + BurstMs;
        m_gapMs = FirstGapMs;
        m_passesFromMs = nowMs;
        m_fixedGap = state == Health::Lost;
    }
    if (static_cast<std::int32_t>(nowMs - m_burstEndMs) >= 0) {
        // Lost: no call on the repeater in use to miss, so the gaps do not
        // grow; unless nothing at all was heard for SilentMs: out of reach
        // for that long, a repeater is not likely to turn up any minute.
        bool heardLately = nowMs - m_passesFromMs < SilentMs;
        for (const HeardList::Entry &e : heard.entries()) {
            heardLately = heardLately || (e.samples > 0 && nowMs - e.measuredMs < SilentMs);
        }
        const bool lost = state == Health::Lost && heardLately;
        m_fixedGap = lost;
        if (nowMs - m_burstEndMs < (lost ? FirstGapMs : m_gapMs)) {
            return false;
        }
        m_burstEndMs = nowMs + BurstMs;
        if (!lost) {
            m_gapMs = std::min(m_gapMs * 2, MaxGapMs);
        }
    }
    return !m_any || nowMs - m_lastMs >= DwellMs;
}

Listening::Bursts Listening::bursts(std::uint32_t nowMs, std::uint32_t &nextMs) const
{
    if (!m_planned) {
        return Bursts::None;
    }
    if (m_scanBurst || (m_scanning && static_cast<std::int32_t>(nowMs - m_burstEndMs) < 0)) {
        return Bursts::Now;
    }
    nextMs = m_scanning ? m_burstEndMs + (m_fixedGap ? FirstGapMs : m_gapMs) : m_scanLastMs + m_scanPeriodMs;
    return Bursts::Later;
}

int Listening::next(const HeardList &heard, std::span<const std::uint32_t> receiveHz, int site, std::uint32_t nowMs)
{
    const std::size_t zoneSize = std::min<std::size_t>(receiveHz.size(), 256);
    const std::uint32_t ownHz = site >= 0 && static_cast<std::size_t>(site) < zoneSize ? receiveHz[site] : 0;
    if (m_passNext >= m_passCount) {
        // A new pass: the current ones strongest first, then the zone.
        m_passCount = 0;
        m_passNext = 0;
        auto add = [&](std::size_t position) {
            if (position >= zoneSize || m_passCount == MaxZone) {
                return;
            }
            const std::uint32_t hz = receiveHz[position];
            if (hz == 0 || hz == ownHz) {
                return;
            }
            for (std::size_t k = 0; k < m_passCount; ++k) {
                if (receiveHz[m_pass[k]] == hz) {
                    return;
                }
            }
            m_pass[m_passCount++] = static_cast<std::uint8_t>(position);
        };
        const HeardList::Entry *sorted[HeardList::Capacity];
        const std::size_t n = strongestFirst(heard, site, nowMs, sorted);
        for (std::size_t k = 0; k < n; ++k) {
            add(sorted[k]->position);
        }
        for (std::size_t position = 0; position < zoneSize; ++position) {
            add(position);
        }
    }
    // A pass made before the zone changed may hold positions it no longer has.
    while (m_passNext < m_passCount) {
        const std::uint8_t position = m_pass[m_passNext++];
        if (position < zoneSize && receiveHz[position] != 0) {
            return position;
        }
    }
    return -1;
}

} // namespace dmm::services::roaming
