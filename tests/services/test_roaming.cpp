// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/roaming.h"

#include <doctest/doctest.h>

#include <array>

using dmm::services::RoamingSettings;
using dmm::services::roaming::choose;
using dmm::services::roaming::Probe;
using dmm::services::roaming::Watch;

TEST_CASE("a search picks the strongest repeater that answered")
{
    const std::array<Probe, 4> probes{{{false, 0}, {true, -95}, {true, -80}, {true, -100}}};
    CHECK(choose(probes, -1, 0) == 2);
    CHECK(choose(probes, 1, 0) == 2);
    const std::array<Probe, 2> none{{{false, -60}, {false, -70}}};
    CHECK(choose(none, -1, 0) == -1);
    CHECK(choose(none, 1, 0) == -1);
    CHECK(choose(std::span<const Probe>(), -1, 0) == -1);
}

TEST_CASE("the repeater in use stays unless another is clearly stronger")
{
    const std::array<Probe, 3> probes{{{true, -90}, {true, -86}, {true, -90}}};
    CHECK(choose(probes, 0, 6) == 0); // 4 dB better: not enough
    CHECK(choose(probes, 0, 4) == 1); // exactly the margin
    CHECK(choose(probes, 2, 0) == 1); // any gain without a margin
    const std::array<Probe, 2> tie{{{true, -90}, {true, -90}}};
    CHECK(choose(tie, 1, 0) == 1); // a tie keeps it
    // The one in use did not answer: the best of the rest.
    const std::array<Probe, 2> lost{{{false, 0}, {true, -110}}};
    CHECK(choose(lost, 0, 6) == 1);
    CHECK(choose(lost, 7, 6) == 1); // out of range counts as none
}

TEST_CASE("an automatic search is due after the repeater went quiet")
{
    RoamingSettings s;
    s.automatic = true;
    s.start = RoamingSettings::Start::OutOfRange;
    s.quietS = 60;
    Watch w;
    w.configure(s, 1000);
    CHECK_FALSE(w.due(1000 + 59'999));
    CHECK(w.due(1000 + 60'000));
    w.heard(50'000);
    CHECK_FALSE(w.due(100'000));
    CHECK(w.due(110'000));
    // A search that found nothing waits as long again.
    w.searched(110'000);
    CHECK_FALSE(w.due(150'000));
    CHECK(w.due(170'000));
    // Too short a quiet time is raised to the minimum.
    s.quietS = 0;
    w.configure(s, 0);
    CHECK_FALSE(w.due(Watch::MinQuietMs - 1));
    CHECK(w.due(Watch::MinQuietMs));
}

TEST_CASE("a periodic search, and none when automatic roaming is off")
{
    RoamingSettings s;
    s.automatic = true;
    s.start = RoamingSettings::Start::Periodic;
    s.periodMinutes = 5;
    Watch w;
    w.configure(s, 0xFFFF'0000u); // across the clock's wrap
    w.heard(0xFFFF'0000u + 10);
    CHECK_FALSE(w.due(0xFFFF'0000u + 299'999));
    CHECK(w.due(0xFFFF'0000u + 300'000));
    s.periodMinutes = 0;
    w.configure(s, 0);
    CHECK(w.due(Watch::MinPeriodMs));
    s.automatic = false;
    w.configure(s, 0);
    CHECK_FALSE(w.automatic());
    CHECK_FALSE(w.due(1'000'000));
}
