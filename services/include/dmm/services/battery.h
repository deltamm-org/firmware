// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/os/os.h"

#include <cstdint>

namespace dmm::services {

/// Battery levels for a pack, in millivolts.
struct BatteryThresholds
{
    /// A bar is shown at or above each of these.
    int bars[3];
    /// Readings must be this much higher to go up a bar, so the level
    /// does not flicker with load.
    int barHysteresis;
    int lowBelow;
    int lowClearsAt;
    int emptyBelow;
    os::Ticks emptyAfterMs;
};

/// The board's battery voltage sense.
class BatterySensor
{
public:
    /// Pack voltage in millivolts before calibration, or -1 without a sense.
    virtual int readMillivolts() = 0;
    /// The levels for the board's battery pack.
    virtual BatteryThresholds thresholds() const = 0;

protected:
    ~BatterySensor() = default;
};

/// Turns periodic voltage readings into a steady voltage, a bar level and
/// low and empty states. All voltages in millivolts.
class BatteryMonitor
{
public:
    /// Readings averaged for the steady voltage.
    static constexpr int Window = 8;

    BatteryMonitor() = default;
    explicit BatteryMonitor(const BatteryThresholds &thresholds);

    /// Levels for the readings from now on. Until set, no level is low or
    /// empty.
    void setThresholds(const BatteryThresholds &thresholds) { m_t = thresholds; }

    /// Calibration offset added to every reading.
    void setOffset(int millivolts) { m_offset = millivolts; }

    /// Feeds one reading taken at @p now (-1 = no sense: nothing changes).
    void update(int millivolts, os::Ticks now);

    bool valid() const { return m_count > 0; }
    int millivolts() const { return m_average; }
    /// 0 (critical) .. 3 (full).
    int level() const { return m_level; }
    bool low() const { return m_low; }
    /// Below emptyBelow for emptyAfterMs without a break.
    bool empty() const { return m_empty; }

private:
    int targetLevel(int mv) const;

    BatteryThresholds m_t{};
    int m_offset = 0;
    int m_samples[Window]{};
    int m_count = 0;
    int m_next = 0;
    int m_average = 0;
    int m_level = 0;
    bool m_low = false;
    bool m_empty = false;
    bool m_belowEmpty = false;
    os::Ticks m_belowEmptySince = 0;
};

} // namespace dmm::services
