// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/codeplug.h"

#include <cstdint>
#include <span>

/// DMR roaming: which repeater of the roaming zone to use, and when to look
/// again.
namespace dmm::services::roaming {

/// What a search found at one repeater.
struct Probe
{
    /// Its timing was found, on the air already or after a wake-up.
    bool answered = false;
    int rssiDbm = 0;
};

/// The repeater to use after a search: the strongest that answered. The one
/// in use (@p current, -1 for none) stays unless another is at least
/// @p marginDb stronger. -1 when none answered.
int choose(std::span<const Probe> probes, int current, int marginDb);

/// When the radio searches by itself, as RoamingSettings ask. Times are
/// milliseconds on a clock that may wrap.
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
