// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/battery.h"

#include <doctest/doctest.h>

using dmm::services::BatteryMonitor;

namespace {

constexpr dmm::services::BatteryThresholds TwoCell{{6450, 6850, 7450}, 100, 6000, 6100, 5950, 10'000};

// Feeds @p count readings of @p mv, 250 ms apart, from @p now on.
void feed(BatteryMonitor &m, int mv, int count, dmm::os::Ticks &now)
{
    for (int i = 0; i < count; ++i) {
        m.update(mv, now);
        now += 250;
    }
}

} // namespace

TEST_CASE("the first reading sets the level; the average follows later ones")
{
    BatteryMonitor m(TwoCell);
    dmm::os::Ticks now = 0;
    CHECK_FALSE(m.valid());
    m.update(-1, now);
    CHECK_FALSE(m.valid());
    feed(m, 7600, 1, now);
    CHECK(m.valid());
    CHECK(m.level() == 3);
    CHECK(m.millivolts() == 7600);
    feed(m, 7000, 1, now);
    CHECK(m.millivolts() == 7300);
}

TEST_CASE("levels go down one bar at a time and up only past the hysteresis")
{
    BatteryMonitor m(TwoCell);
    dmm::os::Ticks now = 0;
    feed(m, 7600, 8, now);
    feed(m, 6300, 8, now); // average falls through every threshold
    CHECK(m.level() == 0);
    feed(m, 6500, 8, now); // above 6.45 V but not 6.55 V
    CHECK(m.level() == 0);
    feed(m, 6560, 8, now);
    CHECK(m.level() == 1);
    feed(m, 6440, 8, now);
    CHECK(m.level() == 0);
}

TEST_CASE("low below 6.00 V, cleared at 6.10 V; the calibration offset counts")
{
    BatteryMonitor m(TwoCell);
    dmm::os::Ticks now = 0;
    feed(m, 5990, 8, now);
    CHECK(m.low());
    feed(m, 6050, 8, now);
    CHECK(m.low());
    feed(m, 6100, 8, now);
    CHECK_FALSE(m.low());
    m.setOffset(-200);
    feed(m, 6100, 8, now);
    CHECK(m.low());
}

TEST_CASE("empty only after 10 s below 5.95 V without a break")
{
    BatteryMonitor m(TwoCell);
    dmm::os::Ticks now = 0;
    feed(m, 5900, 8, now); // 2 s
    CHECK_FALSE(m.empty());
    feed(m, 5900, 33, now); // 10 s after the first low reading
    CHECK(m.empty());
    feed(m, 6200, 8, now);
    CHECK_FALSE(m.empty());
    feed(m, 5900, 20, now); // 5 s
    CHECK_FALSE(m.empty());
}
