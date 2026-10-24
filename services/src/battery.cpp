// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/battery.h"

namespace dmm::services {

BatteryMonitor::BatteryMonitor(const BatteryThresholds &thresholds)
    : m_t(thresholds)
{
}

int BatteryMonitor::targetLevel(int mv) const
{
    int level = 0;
    while (level < 3 && mv >= m_t.bars[level]) {
        ++level;
    }
    return level;
}

void BatteryMonitor::update(int millivolts, os::Ticks now)
{
    if (millivolts < 0) {
        return;
    }
    const bool first = m_count == 0;
    m_samples[m_next] = millivolts + m_offset;
    m_next = (m_next + 1) % Window;
    if (m_count < Window) {
        ++m_count;
    }
    int sum = 0;
    for (int i = 0; i < m_count; ++i) {
        sum += m_samples[i];
    }
    m_average = sum / m_count;

    // The first reading sets the level directly; after that it moves one
    // bar at a time, up only past the hysteresis.
    if (first) {
        m_level = targetLevel(m_average);
    } else if (targetLevel(m_average) < m_level) {
        --m_level;
    } else if (m_level < 3 && m_average >= m_t.bars[m_level] + m_t.barHysteresis) {
        ++m_level;
    }

    if (m_average < m_t.lowBelow) {
        m_low = true;
    } else if (m_average >= m_t.lowClearsAt) {
        m_low = false;
    }

    if (m_average < m_t.emptyBelow) {
        if (!m_belowEmpty) {
            m_belowEmpty = true;
            m_belowEmptySince = now;
        }
        m_empty = static_cast<std::int32_t>(now - m_belowEmptySince) >= static_cast<std::int32_t>(m_t.emptyAfterMs);
    } else {
        m_belowEmpty = false;
        m_empty = false;
    }
}

} // namespace dmm::services
