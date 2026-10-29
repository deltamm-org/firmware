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
    e.levelDbm = clampDbm(known && e.fresh(nowMs) ? (e.levelDbm + levelDbm) / 2 : levelDbm);
    e.measuredMs = nowMs;
}

void HeardList::beacon(std::uint8_t position, int levelDbm, std::uint32_t nowMs)
{
    heard(position, levelDbm, nowMs);
    Entry &e = place(position, nowMs);
    e.misses = 0;
    if (e.beaconMs == 0) {
        e.beaconMs = nowMs;
        return;
    }
    const std::uint32_t since = nowMs - e.beaconMs;
    if (since < MinPeriodMs) {
        // Still the same beacon, or traffic: the beat stays.
        return;
    }
    if (e.periodCs == 0) {
        if (since <= MaxPeriodMs) {
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
            // A shorter period than learned: the learned one spanned a
            // missed beacon.
            e.periodCs = static_cast<std::uint16_t>(since / 10);
        }
        // Otherwise the beacon came late, after the repeater was busy: the
        // period stays and the beat moves to this beacon.
    }
    e.beaconMs = nowMs;
}

void HeardList::missed(std::uint8_t position)
{
    for (std::size_t i = 0; i < m_count; ++i) {
        Entry &e = m_entries[i];
        if (e.position == position && ++e.misses >= MaxMisses) {
            // The period still holds; when the beacon comes is unknown.
            e.misses = 0;
            e.beaconMs = 0;
        }
    }
}

std::uint32_t HeardList::nextBeacon(const Entry &e, std::uint32_t nowMs)
{
    if (e.periodCs == 0 || e.beaconMs == 0) {
        return 0;
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
