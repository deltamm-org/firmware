// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/roaming.h"

#include <algorithm>

namespace dmm::services::roaming {

int choose(std::span<const Probe> probes, int current, int marginDb)
{
    int best = -1;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        if (probes[i].answered && (best < 0 || probes[i].rssiDbm > probes[static_cast<std::size_t>(best)].rssiDbm)) {
            best = static_cast<int>(i);
        }
    }
    const bool currentAnswered = current >= 0 && static_cast<std::size_t>(current) < probes.size() &&
        probes[static_cast<std::size_t>(current)].answered;
    if (best < 0 || !currentAnswered) {
        return best;
    }
    const int gain = probes[static_cast<std::size_t>(best)].rssiDbm - probes[static_cast<std::size_t>(current)].rssiDbm;
    return gain > 0 && gain >= marginDb ? best : current;
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
