// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/roaming.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

using dmm::services::roaming::Activity;
using dmm::services::roaming::Health;
using dmm::services::roaming::HeardList;
using dmm::services::roaming::Levels;
using dmm::services::roaming::Listening;
using dmm::services::roaming::moveTo;
using dmm::services::roaming::Pick;
using dmm::services::roaming::searchOrder;

// The floor of every channel in these tests, in dBm.
constexpr int Floor = -101;
namespace roaming = dmm::services::roaming;

TEST_CASE("the heard list keeps a smoothed level per repeater")
{
    HeardList h;
    h.heard(3, -80, Floor, 1000);
    REQUIRE(h.find(3) != nullptr);
    CHECK(h.find(3)->levelDbm == -80);
    h.heard(3, -90, Floor, 2000); // fresh: averaged
    CHECK(h.find(3)->levelDbm == -85);
    h.heard(3, -100, Floor, 2000 + HeardList::ValidMs); // stale: replaced
    CHECK(h.find(3)->levelDbm == -100);
    CHECK(h.find(4) == nullptr);
    h.heard(4, -300, Floor, 0); // beyond the range is held at its end
    CHECK(h.find(4)->levelDbm == -128);
}

TEST_CASE("a full heard list never drops the repeater in use")
{
    HeardList h;
    h.keep(0);
    for (std::uint8_t i = 0; i < HeardList::Capacity; ++i) {
        h.heard(i, -70, Floor, 1000 + i * 1000u);
    }
    h.heard(40, -60, Floor, 20'000);
    h.heard(41, -60, Floor, 21'000);
    CHECK(h.find(0) != nullptr); // in use, though measured longest ago
    CHECK(h.find(1) == nullptr);
    CHECK(h.find(2) == nullptr);
    h.keep(-1);
    h.heard(42, -60, Floor, 22'000);
    CHECK(h.find(0) == nullptr);
}

TEST_CASE("a full heard list drops the repeater measured longest ago")
{
    HeardList h;
    for (std::uint8_t i = 0; i < HeardList::Capacity; ++i) {
        h.heard(i, -70, Floor, 1000 + i * 1000u);
    }
    h.heard(0, -70, Floor, 20'000); // the oldest is now the second
    h.heard(40, -60, Floor, 21'000);
    CHECK(h.entries().size() == HeardList::Capacity);
    CHECK(h.find(1) == nullptr);
    CHECK(h.find(0) != nullptr);
    CHECK(h.find(40) != nullptr);
}

TEST_CASE("fresh levels are counted")
{
    HeardList h;
    h.heard(1, -80, Floor, 0 + 5);
    h.heard(1, -80, Floor, 1000);
    CHECK(h.find(1)->samples == 2);
    h.heard(1, -80, Floor, 1000 + HeardList::ValidMs);
    CHECK(h.find(1)->samples == 1);
}

TEST_CASE("the search order: the one in use, the heard ones, then the rest in the zone's order")
{
    HeardList h;
    h.heard(6, -90, Floor, 1000);
    h.heard(2, -70, Floor, 1000);
    h.heard(9, -50, Floor, 1000 - HeardList::ValidMs); // an old level
    h.unanswered(5, 1000);                             // never heard: no level
    std::array<std::uint8_t, 64> out{};
    const std::size_t n = searchOrder(h, 4, 10, 1000, out);
    const std::array<std::uint8_t, 10> expected{4, 2, 6, 9, 0, 1, 3, 5, 7, 8};
    REQUIRE(n == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == expected[i]);
    }
    // Positions outside the zone are left out; no repeater in use.
    HeardList far;
    far.heard(30, -40, Floor, 0);
    CHECK(searchOrder(far, -1, 3, 0, out) == 3);
    CHECK(out[0] == 0);
    CHECK(searchOrder(h, -1, 0, 0, out) == 0);
}

TEST_CASE("a search stops at the first good repeater, else takes the strongest, the first of equals")
{
    Pick p(-90);
    CHECK_FALSE(p.tried(0, false, 0));
    CHECK_FALSE(p.tried(1, true, -100));
    CHECK(p.tried(2, true, -85));
    CHECK(p.chosen() == 2);
    Pick q(-90);
    q.tried(3, true, -95); // the one in use, tried first
    q.tried(1, true, -95);
    q.tried(2, false, 0);
    CHECK(q.chosen() == 3);
    q.tried(4, true, -94);
    CHECK(q.chosen() == 4);
    Pick none(-90);
    none.tried(0, false, 0);
    CHECK(none.chosen() == -1);
}

TEST_CASE("the repeater in use: good, weak, quiet or lost")
{
    const Levels l;
    HeardList h;
    CHECK(roaming::health(h, -1, l, 0) == Health::Quiet); // none in use
    CHECK(roaming::health(h, 2, l, 0) == Health::Quiet);  // not heard yet
    h.heard(2, -80, Floor, 1000);
    CHECK(roaming::health(h, 2, l, 1000) == Health::Good);
    h.heard(3, -100, Floor, 1000);
    CHECK(roaming::health(h, 3, l, 1000) == Health::Weak);
    // Silent after being heard well: quiet, not lost; it may just be idle.
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs - 1) == Health::Good);
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs) == Health::Quiet);
    CHECK(roaming::health(h, 2, l, 1000 + 100 * HeardList::ValidMs) == Health::Quiet);
    // Silent after fading below the weak level: lost.
    CHECK(roaming::health(h, 3, l, 1000 + HeardList::ValidMs - 1) == Health::Weak);
    CHECK(roaming::health(h, 3, l, 1000 + HeardList::ValidMs) == Health::Lost);
    h.heard(3, -80, Floor, 300'000); // heard well again
    CHECK(roaming::health(h, 3, l, 300'000) == Health::Good);
}

TEST_CASE("an unanswered wake-up makes the repeater in use lost, until it is heard again")
{
    const Levels l;
    HeardList h;
    h.heard(2, -80, Floor, 1000);
    h.unanswered(2, 2000);
    CHECK(roaming::health(h, 2, l, 2000) == Health::Weak); // heard lately: weak
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs) == Health::Lost);
    h.heard(2, -80, Floor, 200'000);
    CHECK(roaming::health(h, 2, l, 200'000) == Health::Good);
    // Never heard, then unanswered: lost.
    h.unanswered(5, 3000);
    CHECK(roaming::health(h, 5, l, 3000) == Health::Lost);
    CHECK(h.find(5)->samples == 0);
}

TEST_CASE("a repeater heard again without a new level stays current")
{
    const Levels l;
    HeardList h;
    h.heard(2, -80, Floor, 1000);
    // Its bursts keep coming through a long run of calls.
    h.stillHeard(2, 1000 + HeardList::ValidMs - 1);
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs + 5000) == Health::Good);
    CHECK(h.find(2)->levelDbm == -80);
    CHECK(h.find(2)->samples == 1);
    // An old level is not revived.
    h.stillHeard(2, 1000 + 3 * HeardList::ValidMs);
    CHECK(roaming::health(h, 2, l, 1000 + 3 * HeardList::ValidMs) == Health::Quiet);
    h.stillHeard(7, 5000); // not in the list: nothing happens
    CHECK(h.find(7) == nullptr);
}

TEST_CASE("moves: never from good; from weak to a clearly better one; from quiet only to a good one")
{
    const Levels l;
    HeardList h;
    h.heard(0, -98, Floor, 1000); // in use: weak
    h.heard(1, -93, Floor, 1000); // 5 dB better, measured twice
    h.heard(1, -93, Floor, 2000);
    h.heard(2, -80, Floor, 2000); // much better, measured once
    CHECK(moveTo(h, 0, Health::Good, l, 2000) == -1);
    CHECK(moveTo(h, 0, Health::Weak, l, 2000) == -1); // 1 needs 6 dB, 2 a second level
    h.heard(2, -80, Floor, 3000);
    CHECK(moveTo(h, 0, Health::Weak, l, 3000) == 2);
    CHECK(moveTo(h, 0, Health::Weak, l, 3000 + HeardList::ValidMs) == -1); // old levels
    // Quiet: only to a good one, even heard once; none in use counts as quiet.
    HeardList q;
    q.heard(3, Floor + 8, Floor, 5);
    CHECK(moveTo(q, 0, Health::Quiet, l, 5) == -1);
    CHECK(moveTo(q, -1, Health::Quiet, l, 5) == -1);
    q.heard(4, Floor + 10, Floor, 5);
    CHECK(moveTo(q, 0, Health::Quiet, l, 5) == 4);
    // Lost after an unanswered wake-up, or never heard: any current one.
    HeardList g;
    g.heard(3, Floor + 1, Floor, 5);
    CHECK(moveTo(g, 0, Health::Lost, l, 5) == 3);
    g.unanswered(0, 5);
    CHECK(moveTo(g, 0, Health::Lost, l, 5) == 3);
    // Lost after fading out: only to one stronger than it was last heard.
    HeardList f;
    f.heard(0, Floor + 4, Floor, 0);
    f.heard(3, Floor + 3, Floor, 200'000);
    CHECK(moveTo(f, 0, Health::Lost, l, 200'000) == -1);
    f.heard(4, Floor + 5, Floor, 200'000);
    CHECK(moveTo(f, 0, Health::Lost, l, 200'000) == 4);
}

TEST_CASE("levels count in dB above each channel's floor")
{
    const Levels l;
    HeardList h;
    h.heard(0, -90, -101, 1000); // 11 dB above a quiet channel's floor: good
    h.heard(1, -90, -92, 1000);  // 2 dB above a noisy one's: weak
    CHECK(h.find(0)->levelDb() == 11);
    CHECK(h.find(1)->levelDb() == 2);
    CHECK(roaming::health(h, 0, l, 1000) == Health::Good);
    CHECK(roaming::health(h, 1, l, 1000) == Health::Weak);
    // Moves compare levels above each repeater's own floor.
    h.heard(0, -90, -101, 2000);
    CHECK(moveTo(h, 1, Health::Weak, l, 2000) == 0);
}

TEST_CASE("the repeater in use's activity: busy on the air and between overs, idle in a beacon")
{
    Activity a;
    CHECK_FALSE(a.busy(0));
    a.heard(false, 1000); // a beacon starts
    CHECK(a.busy(1000));
    CHECK_FALSE(a.idle(1000 + Activity::SettleMs - 1)); // a call header may still follow
    a.heard(false, 1000 + Activity::SettleMs);
    CHECK(a.idle(1000 + Activity::SettleMs));
    CHECK_FALSE(a.busy(1000 + Activity::SettleMs + Activity::OnAirMs)); // off the air
    // A call: busy until the gap between overs has passed, never idle.
    a.heard(true, 10'000);
    a.heard(false, 12'000); // its hang time
    CHECK_FALSE(a.idle(12'000));
    CHECK(a.busy(10'000 + Activity::CallGapMs - 1));
    CHECK_FALSE(a.busy(10'000 + Activity::CallGapMs));
    // A new transmission starts the settle time again.
    a.heard(false, 30'000);
    CHECK_FALSE(a.idle(30'000));
    a.clear();
    CHECK_FALSE(a.busy(30'000));
}

TEST_CASE("good or weak: one check at a time during the idle transmission, and only below the good level")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 12, Floor, 1000); // clearly good: no listening
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, true, 1000));
    h.heard(1, Floor + 8, Floor, 1000); // good, below the good level
    CHECK_FALSE(v.due(h, 1, l, false, 1000));
    CHECK(v.due(h, 1, l, true, 1000));
    v.done(1000);
    CHECK_FALSE(v.due(h, 1, l, true, 1000 + Listening::HopMs - 1));
    h.heard(2, Floor + 3, Floor, 1000); // weak
    CHECK(v.due(h, 2, l, true, 1000 + Listening::HopMs));
}

TEST_CASE("lost: the gaps between bursts stay at the first")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 3, Floor, 1000); // faded below weak, then silent: lost
    Listening v;
    const std::uint32_t t0 = 1000 + HeardList::ValidMs;
    REQUIRE(roaming::health(h, 0, l, t0) == Health::Lost);
    CHECK(v.due(h, 0, l, false, t0));
    CHECK_FALSE(v.due(h, 0, l, false, t0 + Listening::BurstMs));
    std::uint32_t t = t0 + Listening::BurstMs;
    for (int k = 0; k < 6; ++k) {
        CHECK_FALSE(v.due(h, 0, l, false, t + Listening::FirstGapMs - 1));
        CHECK(v.due(h, 0, l, false, t + Listening::FirstGapMs));
        t += Listening::FirstGapMs + Listening::BurstMs;
    }
}

TEST_CASE("quiet or lost: passes in bursts, with a dwell on the repeater in use between them")
{
    const Levels l;
    const HeardList h; // the one in use not heard: quiet
    Listening v;
    const std::uint32_t t0 = 5000;
    CHECK(v.due(h, 0, l, false, t0));
    v.done(t0 + 4000); // a pass took four seconds
    CHECK_FALSE(v.due(h, 0, l, false, t0 + 4000 + Listening::DwellMs - 1));
    CHECK(v.due(h, 0, l, false, t0 + 4000 + Listening::DwellMs));
    CHECK(v.due(h, 0, l, false, t0 + Listening::BurstMs - 1));
    CHECK_FALSE(v.due(h, 0, l, false, t0 + Listening::BurstMs));
    const std::uint32_t b1 = t0 + Listening::BurstMs + Listening::FirstGapMs;
    CHECK(v.due(h, 0, l, false, b1)); // a burst after the first gap
    CHECK(v.due(h, 0, l, false, b1 + Listening::BurstMs - 1));
    CHECK_FALSE(v.due(h, 0, l, false, b1 + Listening::BurstMs + Listening::FirstGapMs));
    CHECK(v.due(h, 0, l, false, b1 + Listening::BurstMs + 2 * Listening::FirstGapMs)); // the gap doubled
    // The gaps stop growing at the longest.
    std::uint32_t t = b1 + Listening::BurstMs;
    std::uint32_t gap = 2 * Listening::FirstGapMs;
    for (int k = 0; k < 6; ++k) {
        t += gap;
        CHECK(v.due(h, 0, l, false, t));
        t += Listening::BurstMs;
        gap = std::min(gap * 2, Listening::MaxGapMs);
    }
    CHECK(gap == Listening::MaxGapMs);
    CHECK_FALSE(v.due(h, 0, l, false, t + Listening::MaxGapMs - 1));
    CHECK(v.due(h, 0, l, false, t + Listening::MaxGapMs));
    // Heard again, then quiet again: a burst at once.
    HeardList g;
    g.heard(0, Floor + 8, Floor, 2'000'000);
    CHECK(v.due(g, 0, l, true, 2'000'000));
    CHECK(v.due(g, 0, l, false, 2'000'000 + HeardList::ValidMs));
}

TEST_CASE("a pass checks the heard ones strongest first, each frequency once")
{
    HeardList h;
    const std::array<std::uint32_t, 6> hz{438'075'000, 434'575'000, 145'637'500, 434'575'000, 439'000'000, 438'075'000};
    h.heard(1, Floor + 5, Floor, 1000);
    h.heard(2, Floor + 9, Floor, 1000);
    h.heard(3, Floor + 7, Floor, 1000);  // the same frequency as 1
    h.heard(5, Floor + 20, Floor, 1000); // the same frequency as the one in use
    Listening v;
    CHECK(v.next(h, hz, 0, 1000) == 2);
    CHECK(v.next(h, hz, 0, 1000) == 3);
    // 1 shares 3's frequency, 5 the one in use's; then the rest of the zone.
    CHECK(v.next(h, hz, 0, 1000) == 4);
    CHECK(v.next(h, hz, 0, 1000) == 2); // the next pass
    // Nothing to check: a zone of the one in use alone.
    const std::array<std::uint32_t, 1> one{438'075'000};
    Listening w;
    CHECK(w.next(HeardList{}, one, 0, 1000) == -1);
}

TEST_CASE("with none heard, a pass is the zone in its order")
{
    HeardList h;
    std::array<std::uint32_t, 10> hz{};
    for (std::size_t i = 0; i < hz.size(); ++i) {
        hz[i] = 430'000'000 + static_cast<std::uint32_t>(i) * 25'000;
    }
    Listening v;
    // In use: position 1, skipped.
    for (const int expected : {0, 2, 3, 4, 5, 6, 7, 8, 9}) {
        CHECK(v.next(h, hz, 1, 0) == expected);
        CHECK(v.passDone() == (expected == 9));
    }
    CHECK(v.next(h, hz, 1, 0) == 0); // the next pass
    CHECK_FALSE(v.passDone());
}
