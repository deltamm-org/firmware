// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/roaming.h"

#include <doctest/doctest.h>

#include <array>

using dmm::services::RoamingSettings;
using dmm::services::roaming::Health;
using dmm::services::roaming::HeardList;
using dmm::services::roaming::Levels;
using dmm::services::roaming::moveTo;
using dmm::services::roaming::Pick;
using dmm::services::roaming::searchOrder;
using dmm::services::roaming::Visit;
using dmm::services::roaming::Visits;
using dmm::services::roaming::Watch;
namespace roaming = dmm::services::roaming;

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

TEST_CASE("a missed beacon moves the expected one on; two make the beat unknown")
{
    HeardList h;
    h.beacon(5, -60, 1);
    h.beacon(5, -60, 60'001);
    h.missed(5);
    CHECK(HeardList::nextBeacon(*h.find(5), 70'000) == 120'001);
    h.missed(5);
    CHECK(HeardList::nextBeacon(*h.find(5), 70'000) == 0);
    CHECK(h.find(5)->periodMs() == 60'000);
    // The next beacon sets the beat again.
    h.beacon(5, -60, 300'500);
    CHECK(HeardList::nextBeacon(*h.find(5), 301'000) == 360'500);
    h.missed(9); // unknown repeaters are ignored
}

TEST_CASE("a transmission off the beat is not taken as a beacon")
{
    HeardList h;
    h.beacon(4, -60, 1);
    h.beacon(4, -60, 60'001);
    h.beacon(4, -60, 97'001); // 37 s later: another radio's wake-up
    CHECK(h.find(4)->periodMs() == 60'000);
    CHECK(HeardList::nextBeacon(*h.find(4), 98'000) == 120'001);
}

TEST_CASE("fresh levels are counted")
{
    HeardList h;
    h.heard(1, -80, 0 + 5);
    h.heard(1, -80, 1000);
    CHECK(h.find(1)->samples == 2);
    h.heard(1, -80, 1000 + HeardList::ValidMs);
    CHECK(h.find(1)->samples == 1);
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

TEST_CASE("the repeater in use: good, weak, failing, unknown or lost")
{
    const Levels l;
    HeardList h;
    CHECK(roaming::health(h, -1, l, 0) == Health::Lost);
    CHECK(roaming::health(h, 2, l, 0) == Health::Unknown);
    h.heard(2, -80, 1000);
    CHECK(roaming::health(h, 2, l, 1000) == Health::Good);
    h.heard(3, -100, 1000);
    CHECK(roaming::health(h, 3, l, 1000) == Health::Weak);
    h.heard(4, -110, 1000);
    CHECK(roaming::health(h, 4, l, 1000) == Health::Failing);
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs) == Health::Unknown);
    // A beacon every 60 s: the level holds past ValidMs until a miss.
    h.beacon(6, -80, 10'000);
    h.beacon(6, -80, 70'000);
    CHECK(roaming::health(h, 6, l, 70'000 + HeardList::ValidMs + 500) == Health::Good);
    // Beacons stopped: two missed, and nothing heard for two periods.
    h.beacon(5, -70, 10'000);
    h.beacon(5, -70, 70'000);
    h.missed(5);
    h.missed(5);
    CHECK(roaming::health(h, 5, l, 150'000) == Health::Unknown);
    CHECK(roaming::health(h, 5, l, 190'001) == Health::Lost);
}

TEST_CASE("the radio moves on only when the repeater in use weakens, to a clearly better one")
{
    const Levels l;
    HeardList h;
    h.heard(0, -98, 1000); // in use: weak
    h.heard(1, -93, 1000); // 5 dB better, measured twice
    h.heard(1, -93, 2000);
    h.heard(2, -80, 2000); // much better, measured once
    CHECK(moveTo(h, 0, Health::Good, l, 2000) == -1);
    CHECK(moveTo(h, 0, Health::Weak, l, 2000) == -1); // 1 needs 6 dB, 2 a second level
    h.heard(2, -80, 3000);
    CHECK(moveTo(h, 0, Health::Weak, l, 3000) == 2);
    CHECK(moveTo(h, 0, Health::Weak, l, 3000 + HeardList::ValidMs) == -1); // stale
    HeardList f;
    f.heard(0, -108, 0 + 1);
    f.heard(1, -105, 1);
    f.heard(1, -105, 2);
    CHECK(moveTo(f, 0, Health::Failing, l, 2) == 1); // 3 dB is enough
    // Lost: any fresh repeater, even heard once.
    HeardList g;
    g.heard(3, -109, 5);
    CHECK(moveTo(g, 0, Health::Lost, l, 5) == 3);
    CHECK(moveTo(g, -1, Health::Lost, l, 5) == 3);
}

TEST_CASE("a repeater with known timing is visited just before its beacon")
{
    HeardList h;
    h.heard(0, -70, 0 + 1);
    h.beacon(4, -90, 1);
    h.beacon(4, -90, 60'001);
    Visits v;
    // The one in use is good: no visit until the level is RefreshMs old.
    CHECK(v.due(h, 0, 8, Health::Good, 119'800).position != 4);
    const std::uint32_t at = 60'001 + 6 * 60'000 - Visits::LeadMs;
    const Visit t = v.due(h, 0, 8, Health::Good, at);
    CHECK(t.position == 4);
    CHECK_FALSE(t.sweep);
    CHECK(t.beaconMs == 420'001);
    CHECK(t.dwellMs == Visits::LeadMs + Visits::AfterMs);
    // Weak: at the next beacon already.
    CHECK(v.due(h, 0, 8, Health::Weak, 119'800).position == 4);
}

TEST_CASE("sweeps visit the untimed repeaters in turn, later in the period each time")
{
    HeardList h;
    h.heard(0, -70, 1);
    Visits v;
    Visit a = v.due(h, 0, 3, Health::Good, 1000);
    CHECK(a.sweep);
    CHECK(a.position == 1);
    CHECK(a.dwellMs == Visits::SweepDwellMs);
    v.swept(1, Visits::Found::Nothing, 1000, Health::Good);
    CHECK(v.due(h, 0, 3, Health::Good, 1000 + 63'000).position == -1);
    Visit b = v.due(h, 0, 3, Health::Good, 1000 + 63'500);
    CHECK(b.position == 2);
    v.swept(2, Visits::Found::Nothing, 64'500, Health::Good);
    CHECK(v.due(h, 0, 3, Health::Good, 64'500 + 63'500).position == 1); // the one in use is skipped
    CHECK(Visits::sweepGapMs(Health::Lost) < Visits::sweepGapMs(Health::Weak));
    CHECK(Visits::sweepGapMs(Health::Weak) < Visits::sweepGapMs(Health::Good));
}

TEST_CASE("a beacon under way brings a longer visit just before the next one")
{
    HeardList h;
    Visits v;
    REQUIRE(v.due(h, 0, 3, Health::Good, 1000).position == 1);
    v.swept(1, Visits::Found::UnderWay, 1000, Health::Good);
    CHECK(v.due(h, 0, 3, Health::Good, 1000 + Visits::FollowUpAfterMs - 1).position == -1);
    const Visit f = v.due(h, 0, 3, Health::Good, 1000 + Visits::FollowUpAfterMs);
    CHECK(f.position == 1);
    CHECK(f.dwellMs == Visits::FollowUpDwellMs);
    // Under way again: no second follow-up; the sweep moves on.
    v.swept(1, Visits::Found::UnderWay, 57'000, Health::Good);
    CHECK(v.due(h, 0, 3, Health::Good, 57'000 + 63'500).position == 2);
}

TEST_CASE("the range check wakes a quiet repeater and counts unanswered tries")
{
    RoamingSettings s;
    s.rangeCheck = true;
    s.checkIntervalS = 10;
    s.checkTries = 3;
    s.outOfRangeNotices = 2;
    roaming::RangeCheck c;
    c.configure(s, 0);
    CHECK_FALSE(c.due(9'999));
    CHECK(c.due(10'000));
    c.heard(8000); // a beacon: no check yet
    CHECK_FALSE(c.due(10'000));
    CHECK(c.due(18'000));
    CHECK_FALSE(c.checked(false, 18'000));
    CHECK_FALSE(c.due(27'999));
    CHECK_FALSE(c.checked(false, 28'000));
    CHECK_FALSE(c.outOfRange());
    CHECK_FALSE(c.noticeDue(28'000));
    CHECK(c.checked(false, 38'000));
    CHECK(c.outOfRange());
    CHECK(c.noticeDue(38'000));
    CHECK_FALSE(c.noticeDue(40'000)); // once an interval
    CHECK(c.noticeDue(48'000));
    CHECK_FALSE(c.noticeDue(60'000));      // as many as asked
    CHECK_FALSE(c.checked(false, 48'000)); // already out of range
    CHECK_FALSE(c.checked(true, 58'000));
    CHECK_FALSE(c.outOfRange());
    // Off in the codeplug: never due.
    s.rangeCheck = false;
    c.configure(s, 0);
    CHECK_FALSE(c.due(1'000'000));
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

TEST_CASE("with its beacon period known, a level stays fresh a period and the longest beacon")
{
    HeardList h;
    // Beacons exactly 60 s apart count every level as fresh.
    h.beacon(1, -70, 1);
    h.beacon(1, -80, 60'001);
    h.beacon(1, -90, 120'001);
    CHECK(h.find(1)->samples == 3);
    // A period of 120 s.
    h.beacon(2, -70, 1);
    h.beacon(2, -70, 120'001);
    CHECK(h.find(2)->fresh(120'001 + 120'000 + HeardList::MaxBeaconMs - 1));
    CHECK_FALSE(h.find(2)->fresh(120'001 + 120'000 + HeardList::MaxBeaconMs));
}

TEST_CASE("after one beacon, its level stays fresh past the usual period")
{
    HeardList h;
    h.beacon(4, -70, 1000);
    CHECK(h.find(4)->fresh(1000 + 60'000));
    CHECK(h.find(4)->fresh(1000 + HeardList::ValidMs + HeardList::MaxBeaconMs - 1));
    CHECK_FALSE(h.find(4)->fresh(1000 + HeardList::ValidMs + HeardList::MaxBeaconMs));
    h.heard(5, -70, 1000); // heard, no beacon
    CHECK_FALSE(h.find(5)->fresh(1000 + HeardList::ValidMs));
}

TEST_CASE("a beacon's length is kept until another is measured")
{
    HeardList h;
    h.beacon(3, -70, 1, 12'340);
    CHECK(h.find(3)->lengthMs() == 12'340);
    h.beacon(3, -70, 60'001); // length not known: kept
    CHECK(h.find(3)->lengthMs() == 12'340);
    h.beacon(3, -70, 120'001, HeardList::MaxBeaconMs + 10); // too long for a beacon
    CHECK(h.find(3)->lengthMs() == 12'340);
    h.beacon(3, -70, 180'001, 4320);
    CHECK(h.find(3)->lengthMs() == 4320);
}
