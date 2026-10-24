// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/battery.h"

#include <atomic>

/// A battery whose voltage the simulator sets.
class SimBattery final : public dmm::services::BatterySensor
{
public:
    void setMillivolts(int mv) { m_mv = mv; }
    int readMillivolts() override { return m_mv.load(); }
    /// A 2-cell Li-ion pack.
    dmm::services::BatteryThresholds thresholds() const override
    {
        return {{6450, 6850, 7450}, 100, 6000, 6100, 5950, 10'000};
    }

private:
    std::atomic<int> m_mv{7800};
};
