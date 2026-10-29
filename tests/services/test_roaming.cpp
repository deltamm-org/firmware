// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/roaming.h"

#include <doctest/doctest.h>

#include <array>

using dmm::services::RoamingSettings;
using dmm::services::roaming::HeardList;
using dmm::services::roaming::Pick;
using dmm::services::roaming::searchOrder;
using dmm::services::roaming::Watch;

TEST_CASE("the heard list keeps a smoothed level per repeater")
{
    HeardList h;
    h.heard(3, -80, 1000);
    REQUIRE(h.find(3) != nullptr);
    CHECK(h.find(3)->levelDbm == -80);
    h.heard(3, -90, 2000); // fresh: averaged
    CHECK(h.find(3)->levelDbm == -85);
    h.heard(3, -100, 2000 + HeardList::ValidMs); // stale: replaced
    CHECK(h.find(3)->levelDbm == -100);
    CHECK(h.find(4) == nullptr);
    h.heard(4, -300, 0); // beyond the range is held at its end
    CHECK(h.find(4)->levelDbm == -128);
}

TEST_CASE("a full heard list drops the repeater measured longest ago")
{
    HeardList h;
    for (std::uint8_t i = 0; i < HeardList::Capacity; ++i) {
        h.heard(i, -70, 1000 + i * 1000u);
    }
    h.heard(0, -70, 20'000); // the oldest is now the second
    h.heard(40, -60, 21'000);
    CHECK(h.entries().size() == HeardList::Capacity);
    CHECK(h.find(1) == nullptr);
    CHECK(h.find(0) != nullptr);
    CHECK(h.find(40) != nullptr);
}

TEST_CASE("two beacons give the period, and later ones refine it")
{
    HeardList h;
    h.beacon(2, -60, 1'000);
    CHECK(HeardList::nextBeacon(*h.find(2), 2'000) == 0);
    h.beacon(2, -60, 61'020);
    CHECK(h.find(2)->periodMs() == 60'020);
    CHECK(HeardList::nextBeacon(*h.find(2), 62'000) == 121'040);
    // Later beacons, one missed in between, keep the period.
    h.beacon(2, -60, 181'060);
    CHECK(h.find(2)->periodMs() == 60'020);
    // A burst of traffic soon after a beacon leaves the beat alone.
    h.beacon(2, -60, 185'000);
    CHECK(HeardList::nextBeacon(*h.find(2), 186'000) == 241'080);
}

TEST_CASE("a late beacon moves the beat, a shorter interval the period")
{
    HeardList h;
    h.beacon(1, -60, 0 + 1);
    h.beacon(1, -60, 120'001); // the first interval spanned a missed beacon
    CHECK(h.find(1)->periodMs() == 120'000);
    h.beacon(1, -60, 180'001);
    CHECK(h.find(1)->periodMs() == 60'000);
    // Late after traffic: the period stays and the beat follows.
    h.beacon(1, -60, 260'001);
    CHECK(h.find(1)->periodMs() == 60'000);
    CHECK(HeardList::nextBeacon(*h.find(1), 261'000) == 320'001);
}

TEST_CASE("missed beacons make the beat unknown, keeping the period")
{
    HeardList h;
    h.beacon(5, -60, 1);
    h.beacon(5, -60, 60'001);
    h.missed(5);
    CHECK(HeardList::nextBeacon(*h.find(5), 70'000) != 0);
    h.missed(5);
    CHECK(HeardList::nextBeacon(*h.find(5), 70'000) == 0);
    CHECK(h.find(5)->periodMs() == 60'000);
    h.missed(9); // unknown repeaters are ignored
}

TEST_CASE("the search order: the one in use, the heard ones, then the rest")
{
    HeardList h;
    h.heard(6, -90, 1000);
    h.heard(2, -70, 1000);
    h.heard(9, -50, 1000 - HeardList::ValidMs); // stale
    std::array<std::uint8_t, 64> out{};
    const auto o = searchOrder(h, 4, 10, 7, 1000, out);
    const std::array<std::uint8_t, 10> expected{4, 2, 6, 9, 7, 8, 0, 1, 3, 5};
    REQUIRE(o.count == expected.size());
    CHECK(o.restBegins == 4);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == expected[i]);
    }
    // Positions outside the zone are left out; no repeater in use.
    HeardList far;
    far.heard(30, -40, 0);
    const auto p = searchOrder(far, -1, 3, 5, 0, out);
    CHECK(p.count == 3);
    CHECK(p.restBegins == 0);
    CHECK(out[0] == 2);
    CHECK(searchOrder(h, -1, 0, 0, 0, out).count == 0);
}

TEST_CASE("a search stops at the first good repeater")
{
    Pick p(-1, -90, 0);
    CHECK_FALSE(p.tried(0, false, 0));
    CHECK_FALSE(p.tried(1, true, -100));
    CHECK(p.tried(2, true, -85));
    CHECK(p.chosen() == 2);
    // None good: the strongest that answered.
    Pick q(-1, -90, 0);
    q.tried(0, true, -105);
    q.tried(1, true, -95);
    q.tried(2, false, 0);
    CHECK(q.chosen() == 1);
    Pick none(-1, -90, 0);
    none.tried(0, false, 0);
    CHECK(none.chosen() == -1);
}

TEST_CASE("the repeater in use stays when good, or unless another is clearly stronger")
{
    Pick good(3, -90, 6);
    CHECK(good.tried(3, true, -80)); // in good coverage the search ends at once
    CHECK(good.chosen() == 3);
    Pick weak(3, -90, 6);
    CHECK_FALSE(weak.tried(3, true, -100));
    CHECK_FALSE(weak.tried(1, true, -96)); // 4 dB better: not enough
    CHECK(weak.chosen() == 3);
    CHECK(weak.tried(2, true, -88)); // good and 12 dB better
    CHECK(weak.chosen() == 2);
    Pick tie(3, -90, 0);
    tie.tried(3, true, -95);
    tie.tried(1, true, -95);
    CHECK(tie.chosen() == 3); // a tie keeps it
    Pick lost(3, -90, 6);
    lost.tried(3, false, 0);
    lost.tried(1, true, -110);
    CHECK(lost.chosen() == 1); // the one in use did not answer
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
