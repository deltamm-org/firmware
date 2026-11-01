// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <atomic>
#include <cstdint>

namespace dmm::services {

/// Notices the watchdog going unfed before the watchdog itself restarts
/// the radio, so the stall can be recorded. The task that feeds the
/// watchdog calls fed(); a 1 ms tick interrupt calls tick(). Nothing is
/// noticed before the first feed.
class StallWatch
{
public:
    explicit constexpr StallWatch(std::uint32_t limitMs)
        : m_limitMs(limitMs)
    {
    }

    void fed()
    {
        m_sinceFeedMs.store(0, std::memory_order_relaxed);
        m_armed.store(true, std::memory_order_relaxed);
    }

    /// One millisecond passed; true once no feed came for the limit, and
    /// on every tick after that until the next feed.
    bool tick()
    {
        if (!m_armed.load(std::memory_order_relaxed)) {
            return false;
        }
        const std::uint32_t ms = m_sinceFeedMs.load(std::memory_order_relaxed) + 1;
        m_sinceFeedMs.store(ms, std::memory_order_relaxed);
        return ms >= m_limitMs;
    }

    std::uint32_t sinceFeedMs() const { return m_sinceFeedMs.load(std::memory_order_relaxed); }

private:
    std::uint32_t m_limitMs;
    std::atomic<std::uint32_t> m_sinceFeedMs{0};
    std::atomic<bool> m_armed{false};
};

} // namespace dmm::services
