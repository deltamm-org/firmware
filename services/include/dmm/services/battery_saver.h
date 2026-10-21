// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// Battery save while receiving: once the radio has been idle for
/// StartAfterMs, the receiver sleeps for the mode's off time, then listens
/// for ListenMs, and so on. Activity, a held stay-awake reason or a
/// receiver that is not receiving ends it at once.
class BatterySaver
{
public:
    static constexpr std::uint32_t StartAfterMs = 5000;
    static constexpr std::uint32_t ListenMs = 180;

    enum class Action : std::uint8_t
    {
        None,
        Sleep, // switch the receiver off
        Wake,  // switch it back on
    };

    /// 0 = off; 1 or 2 = the receiver sleeps 150 or 70 ms per cycle.
    void setMode(std::uint8_t mode) { m_mode = mode <= 2 ? mode : 1; }
    std::uint8_t mode() const { return m_mode; }

    static constexpr std::uint32_t offMs(std::uint8_t mode) { return mode == 2 ? 70 : 150; }

    /// One step at @p nowMs. @p idleMs: how long the radio has been idle
    /// (0 while busy). @p receiverOn: the receiver is receiving or asleep
    /// in battery save.
    Action update(std::uint32_t nowMs, std::uint32_t idleMs, bool receiverOn)
    {
        if (m_mode == 0 || !receiverOn || idleMs < StartAfterMs) {
            const bool wasAsleep = m_asleep;
            m_active = false;
            m_asleep = false;
            return wasAsleep && receiverOn ? Action::Wake : Action::None;
        }
        if (!m_active) {
            m_active = true;
            return sleep(nowMs);
        }
        if (static_cast<std::int32_t>(nowMs - m_until) < 0) {
            return Action::None;
        }
        if (m_asleep) {
            m_asleep = false;
            m_until = nowMs + ListenMs;
            return Action::Wake;
        }
        return sleep(nowMs);
    }

    /// Forgets the cycle after the receiver was woken or retuned elsewhere;
    /// the next update() may start a new one.
    void reset()
    {
        m_active = false;
        m_asleep = false;
    }

    /// Cycling: the receiver sleeps and listens in turn.
    bool active() const { return m_active; }
    bool asleep() const { return m_asleep; }
    /// When update() next has something to do while active.
    std::uint32_t nextChangeMs() const { return m_until; }
    /// Sleeps since start.
    std::uint32_t cycles() const { return m_cycles; }

private:
    Action sleep(std::uint32_t nowMs)
    {
        m_asleep = true;
        m_until = nowMs + offMs(m_mode);
        ++m_cycles;
        return Action::Sleep;
    }

    std::uint8_t m_mode = 0;
    bool m_active = false;
    bool m_asleep = false;
    std::uint32_t m_until = 0;
    std::uint32_t m_cycles = 0;
};

} // namespace dmm::services
