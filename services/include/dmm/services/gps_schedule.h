// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// Decides when the GPS receiver runs. Each client says how often it needs
/// a fresh position; with power saving on, the receiver is switched off
/// between fixes when the shortest need is long enough, and switched on
/// again early enough for a fix to be ready when it is due. All times are
/// milliseconds on one monotonic clock.
class GpsSchedule
{
public:
    enum class Client : std::uint8_t
    {
        Clock,   // keeps the real-time clock right
        Screen,  // the GPS screen is open
        Console, // the console's gps need, for tests
        Aprs,    // position beacons
        Count,
    };

    enum class Action : std::uint8_t
    {
        None,
        PowerOn,
        PowerOff,
    };

    /// Needs at least this short keep the receiver on.
    static constexpr std::uint32_t ContinuousUpToMs = 180'000;
    /// The time a start is expected to take before the first measurement,
    /// and the bounds of the lead time learnt from measurements.
    static constexpr std::uint32_t DefaultLeadMs = 150'000;
    static constexpr std::uint32_t MinLeadMs = 30'000;
    static constexpr std::uint32_t MaxLeadMs = 300'000;
    /// A start without a fix by then is given up until the next try.
    static constexpr std::uint32_t GiveUpMs = 300'000;
    /// The receiver stays on this long after the first fix of a start, so
    /// the whole second's sentences arrive and the position settles.
    static constexpr std::uint32_t SettleMs = 10'000;
    /// Never sleep for less than this.
    static constexpr std::uint32_t MinSleepMs = 60'000;

    /// @p intervalMs between fresh positions; 0 = this client needs none.
    void setNeed(Client client, std::uint32_t intervalMs);
    /// Off: the receiver stays on whenever the GPS is enabled.
    void setPowerSave(bool on) { m_powerSave = on; }
    /// The user's GPS switch. Disabling switches the receiver off.
    void setEnabled(bool on);

    /// A position fix arrived at @p now.
    void fix(std::uint32_t now);

    /// What to do with the receiver now, given whether it is powered.
    Action update(std::uint32_t now, bool powered);

    /// The receiver sleeps by choice: the GPS is enabled but switched off.
    bool asleep(bool powered) const { return m_enabled && !powered; }
    /// The shortest need, or 0 when no client needs a position.
    std::uint32_t shortestNeed() const;
    bool continuous() const;
    std::uint32_t leadMs() const { return m_leadMs; }
    /// When a sleeping receiver is switched on next; meaningful only while
    /// asleep and some client has a need.
    std::uint32_t wakeAt() const;

private:
    Action powerOn(std::uint32_t now);
    Action powerOff(std::uint32_t now);

    std::uint32_t m_needs[static_cast<int>(Client::Count)] = {};
    bool m_powerSave = false;
    bool m_enabled = false;
    std::uint32_t m_wokeAt = 0;
    bool m_fixSinceWake = false;
    std::uint32_t m_firstFixAt = 0;
    std::uint32_t m_lastFixAt = 0;
    bool m_everFixed = false;
    std::uint32_t m_sleptAt = 0;
    // After a start without a fix, the next try waits until m_retryAt;
    // m_tryNow starts the first try at once.
    std::uint32_t m_retryAt = 0;
    bool m_retryPending = false;
    bool m_tryNow = false;
    std::uint32_t m_leadMs = DefaultLeadMs;
};

} // namespace dmm::services
