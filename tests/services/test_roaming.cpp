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

TEST_CASE("the level is the mean of the last three readings, none older than ValidMs")
{
    HeardList h;
    h.heard(3, -80, Floor, 1000);
    REQUIRE(h.find(3) != nullptr);
    CHECK(h.find(3)->levelDbm() == -80);
    h.heard(3, -90, Floor, 6000);
    CHECK(h.find(3)->levelDbm() == -85);
    h.heard(3, -91, Floor, 11'000);
    CHECK(h.find(3)->levelDbm() == -87);
    h.heard(3, -92, Floor, 16'000); // the first gives way
    CHECK(h.find(3)->levelDbm() == -91);
    CHECK(h.find(3)->readings == 3);
    // Beacons a minute apart: the one two minutes old still counts.
    h.heard(3, -70, Floor, 76'000);
    h.heard(3, -76, Floor, 136'000);
    CHECK(h.find(3)->readings == 3);
    CHECK(h.find(3)->levelDbm() == -79);
    // A beacon missed: the one ValidMs old no longer counts.
    h.heard(3, -82, Floor, 76'000 + HeardList::ValidMs);
    CHECK(h.find(3)->readings == 2);
    CHECK(h.find(3)->levelDbm() == -79);
    h.heard(3, -100, Floor, 211'000 + HeardList::ValidMs); // stale: replaced
    CHECK(h.find(3)->levelDbm() == -100);
    CHECK(h.find(3)->readings == 1);
    CHECK(h.find(4) == nullptr);
    h.heard(4, -300, Floor, 0); // beyond the range is held at its end
    CHECK(h.find(4)->levelDbm() == -128);
}

TEST_CASE("a steady level is reached from either side")
{
    HeardList h;
    h.heard(1, -94, Floor, 1000);
    std::uint32_t t = 11'000;
    for (; t < 300'000; t += 10'000) {
        h.heard(1, -95, Floor, t);
    }
    CHECK(h.find(1)->levelDbm() == -95);
    for (; t < 600'000; t += 10'000) {
        h.heard(1, -94, Floor, t);
    }
    CHECK(h.find(1)->levelDbm() == -94);
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

TEST_CASE("a repeater that never answered leaves the heard list once another is in use")
{
    HeardList h;
    h.heard(1, -70, Floor, 1000);
    h.keep(0);
    h.unanswered(0, 2000); // in use, woken, never heard
    h.keep(0);
    REQUIRE(h.find(0) != nullptr);
    CHECK(h.find(0)->unanswered);
    h.keep(1); // a search moved to repeater 1
    CHECK(h.find(0) == nullptr);
    CHECK(h.find(1) != nullptr);
    // One that was heard stays, with its level, after it did not answer.
    h.unanswered(1, 3000);
    h.keep(2);
    REQUIRE(h.find(1) != nullptr);
    CHECK(h.find(1)->levelDbm() == -70);
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

TEST_CASE("the repeater in use: strong, good, fair, quiet or lost")
{
    const Levels l;
    HeardList h;
    CHECK(roaming::health(h, -1, l, 0) == Health::Quiet); // none in use
    CHECK(roaming::health(h, 2, l, 0) == Health::Quiet);  // not heard yet
    h.heard(2, -88, Floor, 1000);
    CHECK(roaming::health(h, 2, l, 1000) == Health::Good);
    h.heard(3, -100, Floor, 1000);
    CHECK(roaming::health(h, 3, l, 1000) == Health::Fair);
    h.heard(4, -80, Floor, 1000);
    CHECK(roaming::health(h, 4, l, 1000) == Health::Strong);
    // Silent after being heard well: quiet, not lost; it may just be idle.
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs - 1) == Health::Good);
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs) == Health::Quiet);
    CHECK(roaming::health(h, 2, l, 1000 + 100 * HeardList::ValidMs) == Health::Quiet);
    // Silent after fading below the fair level: lost.
    CHECK(roaming::health(h, 3, l, 1000 + HeardList::ValidMs - 1) == Health::Fair);
    CHECK(roaming::health(h, 3, l, 1000 + HeardList::ValidMs) == Health::Lost);
    h.heard(3, -88, Floor, 300'000); // heard well again
    CHECK(roaming::health(h, 3, l, 300'000) == Health::Good);
}

TEST_CASE("strong and below fair each end only backDb past their level")
{
    const Levels l;
    HeardList h;
    std::uint32_t t = 1000;
    // Three readings at @p db: the level is @p db.
    auto steady = [&](int db) {
        for (int k = 0; k < 3; ++k) {
            t += 5000;
            h.heard(1, Floor + db, Floor, t, l);
        }
        return roaming::health(h, 1, l, t);
    };
    CHECK(steady(l.strongDb) == Health::Strong);
    CHECK(steady(l.strongDb - l.backDb) == Health::Strong);
    CHECK(steady(l.strongDb - l.backDb - 1) == Health::Good);
    CHECK(steady(l.strongDb - 1) == Health::Good);
    CHECK(steady(l.strongDb) == Health::Strong);
    CHECK(steady(l.fairDb) == Health::Good);
    CHECK(steady(l.fairDb - 1) == Health::Fair);
    CHECK(steady(l.fairDb + l.backDb - 1) == Health::Fair);
    CHECK(steady(l.fairDb + l.backDb) == Health::Good);
    // An old level is judged afresh.
    CHECK(steady(l.fairDb - 1) == Health::Fair);
    t += HeardList::ValidMs;
    h.heard(1, Floor + l.fairDb, Floor, t, l);
    CHECK(roaming::health(h, 1, l, t) == Health::Good);
}

TEST_CASE("an unanswered wake-up makes the repeater in use lost, until it is heard again")
{
    const Levels l;
    HeardList h;
    h.heard(2, -88, Floor, 1000);
    h.unanswered(2, 2000);
    CHECK(roaming::health(h, 2, l, 2000) == Health::Fair); // heard lately: fair
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs) == Health::Lost);
    h.heard(2, -88, Floor, 200'000);
    CHECK(roaming::health(h, 2, l, 200'000) == Health::Good);
    // Never heard, then unanswered: lost.
    h.unanswered(5, 3000);
    CHECK(roaming::health(h, 5, l, 3000) == Health::Lost);
    CHECK(h.find(5)->samples == 0);
}

TEST_CASE("a repeater still on the air stays current")
{
    const Levels l;
    HeardList h;
    h.heard(2, -88, Floor, 1000);
    // A lone burst keeps it current; as a reading it is too short.
    h.stillHeard(2, -70, Floor, 1500);
    h.flush(1500 + HeardList::ReadingGapMs);
    CHECK(h.find(2)->samples == 1);
    h.stillHeard(2, -88, Floor, 1000 + HeardList::ValidMs - 1);
    CHECK(roaming::health(h, 2, l, 1000 + HeardList::ValidMs + 5000) == Health::Good);
    // That burst began a reading; one burst is too few for a current level.
    h.flush(1000 + HeardList::ValidMs - 1 + HeardList::ReadingGapMs);
    CHECK(h.find(2)->samples == 1);
    CHECK(h.find(2)->levelDbm() == -88);
}

TEST_CASE("a reading is the mean of a transmission's bursts over 5 s, one after another")
{
    HeardList h;
    h.heard(1, -80, Floor, 0);
    const std::uint32_t t0 = 60'000;
    h.started(1, -68, Floor, t0);
    int k = 1;
    std::uint32_t t = t0 + 60;
    for (; t - t0 < HeardList::ReadingSpanMs; t += 60) {
        h.stillHeard(1, (k++ % 2) != 0 ? -72 : -68, Floor, t);
    }
    CHECK(h.find(1)->samples == 1); // not yet
    h.stillHeard(1, -72, Floor, t); // the span has passed: -70
    CHECK(h.find(1)->samples == 2);
    CHECK(h.find(1)->levelDbm() == -75);
    // The transmission goes on: the next reading begins with its next burst.
    const std::uint32_t read = t;
    for (t += 60; h.find(1)->samples == 2 && t - read < 2 * HeardList::ReadingSpanMs; t += 60) {
        h.stillHeard(1, -100, Floor, t);
    }
    CHECK(t - read > HeardList::ReadingSpanMs);
    CHECK(t - read < HeardList::ReadingSpanMs + 200);
    CHECK(h.find(1)->samples == 3);
    CHECK(h.find(1)->levelDbm() == -83);
    // Then one, cut short by the end of the transmission: 8 bursts are enough.
    for (int n = 0; n < HeardList::ReadingBursts; ++n, t += 60) {
        h.stillHeard(1, -100, Floor, t);
    }
    h.flush(t + HeardList::ReadingGapMs);
    CHECK(h.find(1)->samples == 4);
    CHECK(h.find(1)->levelDbm() == -90);
    // Fewer are dropped while the level is current.
    h.started(1, -100, Floor, t + 1000);
    h.stillHeard(1, -100, Floor, t + 1060);
    h.flush(t + 1060 + HeardList::ReadingGapMs);
    CHECK(h.find(1)->samples == 4);
    // Not current: taken however short.
    h.started(1, -90, Floor, t + 2000 + HeardList::ValidMs);
    h.flush(t + 2000 + HeardList::ValidMs + HeardList::ReadingGapMs);
    CHECK(h.find(1)->levelDbm() == -90);
    // Bursts of one not listed begin a reading of it.
    h.stillHeard(5, -77, Floor, t + 3000 + HeardList::ValidMs);
    h.flush(t + 3000 + HeardList::ValidMs + HeardList::ReadingGapMs);
    REQUIRE(h.find(5) != nullptr);
    CHECK(h.find(5)->levelDbm() == -77);
}

TEST_CASE("a check during a transmission only pauses its reading")
{
    HeardList h;
    h.heard(1, -80, Floor, 0);
    const std::uint32_t t0 = 60'000;
    // A beacon: bursts for a second, then a check of another repeater.
    h.started(1, -90, Floor, t0);
    std::uint32_t t = t0 + 60;
    for (; t < t0 + 1000; t += 60) {
        h.stillHeard(1, -90, Floor, t);
    }
    h.pause();
    h.flush(t + 2 * HeardList::ReadingGapMs); // away: not ended
    CHECK(h.find(1)->samples == 1);
    // Back on it while it goes on: the same reading.
    t += 3 * HeardList::ReadingGapMs;
    h.started(1, -70, Floor, t);
    for (t += 60; t < t0 + 3000; t += 60) {
        h.stillHeard(1, -70, Floor, t);
    }
    h.flush(t + HeardList::ReadingGapMs); // paused once: it ends with its span
    CHECK(h.find(1)->samples == 1);
    h.flush(t0 + HeardList::ReadingSpanMs);
    CHECK(h.find(1)->samples == 2);
    CHECK(h.find(1)->levelDbm() == -80); // the mean of the two, not -70
    // Paused and not heard again: taken once its span has passed.
    const std::uint32_t t1 = t0 + 60'000;
    h.started(1, -100, Floor, t1);
    for (t = t1 + 60; t < t1 + 1000; t += 60) {
        h.stillHeard(1, -100, Floor, t);
    }
    h.pause();
    h.flush(t1 + HeardList::ReadingSpanMs - 1);
    CHECK(h.find(1)->samples == 2);
    h.flush(t1 + HeardList::ReadingSpanMs);
    CHECK(h.find(1)->samples == 3);
    // Back after its span: a new reading, the old one taken.
    const std::uint32_t t2 = t1 + 60'000;
    h.started(1, -100, Floor, t2);
    for (t = t2 + 60; t < t2 + 1000; t += 60) {
        h.stillHeard(1, -100, Floor, t);
    }
    h.pause();
    h.started(1, -60, Floor, t2 + HeardList::ReadingSpanMs);
    CHECK(h.find(1)->samples == 4);
}

TEST_CASE("a beacon with a check every half second stays one reading")
{
    HeardList h;
    h.heard(1, -60, Floor, 0);
    const std::uint32_t t0 = 60'000;
    h.started(1, -90, Floor, t0);
    std::uint32_t t = t0;
    // Back each time: a burst, then its slot timing found again, then a
    // few more bursts, then the next check.
    for (int piece = 0; piece < 8; ++piece) {
        h.stillHeard(1, -90, Floor, t + 20);
        h.started(1, -90, Floor, t + 120);
        for (std::uint32_t b = t + 150; b < t + 350; b += 30) {
            h.stillHeard(1, -90, Floor, b);
        }
        h.pause();
        h.flush(t + 500);
        t += 530;
    }
    // Its slot timing found first restarted it, 120 ms in.
    h.flush(t0 + 120 + HeardList::ReadingSpanMs - 1);
    CHECK(h.find(1)->samples == 1);
    h.flush(t0 + 120 + HeardList::ReadingSpanMs);
    CHECK(h.find(1)->samples == 2);
    CHECK(h.find(1)->levelDbm() == -75); // the mean of -60 and one -90
}

TEST_CASE("a long call fading out turns fair within seconds, then poor")
{
    const Levels l;
    HeardList h;
    h.heard(1, -63, Floor, 1000); // the call starts strong
    // A burst every 60 ms while it has faded to -99 dBm.
    std::uint32_t t = 1060;
    for (; roaming::health(h, 1, l, t) != Health::Fair && t < 600'000; t += 60) {
        h.stillHeard(1, -99, Floor, t);
        h.flush(t);
    }
    CHECK(t > 10'000);
    CHECK(t < 20'000);
    // Below the fair level for as long as a level stays current: poor.
    const std::uint32_t fair = t;
    for (; t <= fair + HeardList::ValidMs; t += 60) {
        h.stillHeard(1, -99, Floor, t);
        h.flush(t);
    }
    CHECK(roaming::health(h, 1, l, t) == Health::Poor);
}

TEST_CASE("moves: never from strong; from good or fair to a clearly better one; from quiet only to a good one")
{
    const Levels l;
    HeardList h;
    h.heard(0, -98, Floor, 1000); // in use: weak
    h.heard(1, -93, Floor, 1000); // 5 dB better, measured twice
    h.heard(1, -93, Floor, 2000);
    h.heard(2, -88, Floor, 2000); // much better, measured once
    CHECK(moveTo(h, 0, Health::Strong, l, 2000) == -1);
    CHECK(moveTo(h, 0, Health::Good, l, 2000) == -1);
    CHECK(moveTo(h, 0, Health::Fair, l, 2000) == -1); // 1 needs 6 dB, 2 a second level
    h.heard(2, -88, Floor, 3000);
    CHECK(moveTo(h, 0, Health::Fair, l, 3000) == 2);
    CHECK(moveTo(h, 0, Health::Good, l, 3000) == 2);
    CHECK(moveTo(h, 0, Health::Strong, l, 3000) == -1);
    CHECK(moveTo(h, 0, Health::Fair, l, 3000 + HeardList::ValidMs) == -1); // old levels
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
    CHECK(roaming::health(h, 1, l, 1000) == Health::Fair);
    // Moves compare levels above each repeater's own floor.
    h.heard(0, -90, -101, 2000);
    CHECK(moveTo(h, 1, Health::Fair, l, 2000) == 0);
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

TEST_CASE("a call holds listening off until the gap between overs has passed from its end")
{
    Activity a;
    a.heard(true, 1000); // the call's header
    for (std::uint32_t t = 2000; t <= 12'000; t += 1000) {
        a.heard(false, t); // its timing keeps it on the air; no burst of the call seen
    }
    CHECK(a.idle(12'000)); // by its bursts alone the call no longer counts
    a.callHeard(12'000);   // the radio still hears the call, or it has just ended
    CHECK_FALSE(a.idle(12'000));
    for (std::uint32_t t = 13'000; t < 12'000 + Activity::CallGapMs; t += 1000) {
        a.heard(false, t);
        CHECK_FALSE(a.idle(t));
    }
    a.heard(false, 12'000 + Activity::CallGapMs);
    CHECK(a.idle(12'000 + Activity::CallGapMs));
}

TEST_CASE("good or fair: one check at a time during the idle transmission; none while strong")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 22, Floor, 1000); // strong: no listening
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, true, 1000));
    h.heard(1, Floor + 12, Floor, 1000); // good
    CHECK_FALSE(v.due(h, 1, l, false, 1000));
    CHECK(v.due(h, 1, l, true, 1000));
    v.done(1000);
    CHECK_FALSE(v.due(h, 1, l, true, 1000 + Listening::HopMs - 1));
    h.heard(2, Floor + 3, Floor, 1000); // fair
    CHECK(v.due(h, 2, l, true, 1000 + Listening::HopMs));
}

TEST_CASE("a pass made before the zone changed skips positions the zone no longer has")
{
    const HeardList h;
    Listening v;
    const std::uint32_t five[] = {438'000'000, 438'100'000, 438'200'000, 438'300'000, 438'400'000};
    CHECK(v.next(h, five, 0, 0) == 1); // the frequency in use is left out
    const std::uint32_t two[] = {438'000'000, 438'100'000};
    CHECK(v.next(h, two, 0, 0) == -1); // 2, 3 and 4 are gone
    CHECK(v.passDone());
}

TEST_CASE("another zone: a new pass starts in the new zone's order")
{
    const HeardList h;
    Listening v;
    const std::uint32_t three[] = {438'000'000, 438'100'000, 438'200'000};
    CHECK(v.next(h, three, 0, 0) == 1);
    // The same repeaters in another order: the old pass would give 2 next.
    const std::uint32_t reordered[] = {438'200'000, 438'000'000, 438'100'000};
    v.restartPass();
    CHECK(v.next(h, reordered, 1, 0) == 0);
    CHECK(v.next(h, reordered, 1, 0) == 2);
    CHECK(v.passDone());
}

TEST_CASE("another zone: the heard list follows its repeaters to their new positions")
{
    HeardList h;
    h.heard(0, Floor + 20, Floor, 1000);
    h.heard(1, Floor + 12, Floor, 1000);
    h.heard(2, Floor + 8, Floor, 1000);
    h.keep(2);
    // 0 moves to 2, 1 is not in the new zone, 2 moves to 0; 3 was never there.
    const std::int16_t moved[] = {2, -1, 0};
    h.renumber(moved);
    REQUIRE(h.entries().size() == 2);
    REQUIRE(h.find(2) != nullptr);
    CHECK(h.find(2)->levelDb() == 20);
    REQUIRE(h.find(0) != nullptr);
    CHECK(h.find(0)->levelDb() == 8);
    CHECK(h.find(1) == nullptr);
    // The one in use, now at 0, is never dropped from a full list.
    for (std::uint8_t p = 10; p < 10 + HeardList::Capacity; ++p) {
        h.heard(p, Floor + 30, Floor, 2000 + p);
    }
    CHECK(h.find(0) != nullptr);
}

TEST_CASE("good: a burst of passes every ScanGoodMs, not started while busy; none while strong")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 12, Floor, 1000); // good
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, false, 1000)); // not idle, no burst yet
    CHECK_FALSE(v.passing());
    const std::uint32_t b = 1000 + Listening::ScanGoodMs;
    h.heard(0, Floor + 12, Floor, b - 1000);
    CHECK_FALSE(v.due(h, 0, l, false, b, true)); // busy: the burst waits
    CHECK(v.due(h, 0, l, false, b + 500));
    CHECK(v.passing());
    v.done(b + 1500); // a pass
    CHECK_FALSE(v.due(h, 0, l, false, b + 1500 + Listening::DwellMs - 1));
    CHECK(v.due(h, 0, l, false, b + 1500 + Listening::DwellMs));
    CHECK_FALSE(v.due(h, 0, l, false, b + 2500, true)); // busy: no pass
    CHECK(v.due(h, 0, l, false, b + 500 + Listening::BurstMs - 1));
    CHECK_FALSE(v.due(h, 0, l, false, b + 500 + Listening::BurstMs)); // the burst is over
    CHECK_FALSE(v.passing());
    h.heard(0, Floor + 12, Floor, b + 500 + Listening::ScanGoodMs - 1000);
    CHECK_FALSE(v.due(h, 0, l, false, b + 500 + Listening::ScanGoodMs - 1));
    CHECK(v.due(h, 0, l, false, b + 500 + Listening::ScanGoodMs)); // the next one
    // Strong: no bursts.
    HeardList e;
    e.heard(0, Floor + 22, Floor, 1000);
    Listening w;
    CHECK_FALSE(w.due(e, 0, l, false, 1000));
    e.heard(0, Floor + 22, Floor, 1000 + Listening::ScanGoodMs);
    CHECK_FALSE(w.due(e, 0, l, false, 1000 + Listening::ScanGoodMs));
}

TEST_CASE("good again after a burst while quiet: the next burst ScanGoodMs after that one")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 12, Floor, 1000); // good
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, false, 1000));
    // Last heard so that it turns quiet when the good burst would be due.
    const std::uint32_t q = 1000 + Listening::ScanGoodMs;
    h.heard(0, Floor + 12, Floor, q - HeardList::ValidMs);
    REQUIRE(roaming::health(h, 0, l, q) == Health::Quiet);
    CHECK(v.due(h, 0, l, false, q)); // a burst while quiet
    CHECK(v.passing());
    // Heard again, good, just after that burst: no second burst at once.
    const std::uint32_t g = q + Listening::BurstMs + 10'000;
    h.heard(0, Floor + 12, Floor, g);
    REQUIRE(roaming::health(h, 0, l, g) == Health::Good);
    CHECK_FALSE(v.due(h, 0, l, false, g));
    CHECK_FALSE(v.passing());
    h.heard(0, Floor + 12, Floor, q + Listening::ScanGoodMs - 1000);
    CHECK_FALSE(v.due(h, 0, l, false, q + Listening::ScanGoodMs - 1));
    CHECK(v.due(h, 0, l, false, q + Listening::ScanGoodMs));
    CHECK(v.passing());
}

TEST_CASE("fair: a burst every ScanFairMs, so turning fair brings the next one closer")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 12, Floor, 1000); // good
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, false, 1000));
    const std::uint32_t t = 1000 + Listening::ScanFairMs;
    h.heard(0, Floor + 12, Floor, t - 61'000);
    CHECK_FALSE(v.due(h, 0, l, false, t));  // still good: not yet
    h.heard(0, Floor - 4, Floor, t - 1000); // the mean of the two, 4: fair
    REQUIRE(roaming::health(h, 0, l, t) == Health::Fair);
    CHECK(v.due(h, 0, l, false, t));
    CHECK(v.passing());
}

TEST_CASE("bursts: the next one starts though the last was not seen to end; a move ends one")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 12, Floor, 1000);
    Listening v;
    CHECK_FALSE(v.due(h, 0, l, false, 1000));
    const std::uint32_t b = 1000 + Listening::ScanGoodMs;
    h.heard(0, Floor + 12, Floor, b - 1000);
    CHECK(v.due(h, 0, l, false, b));
    // Not asked again until the next burst is due: it still starts.
    h.heard(0, Floor + 12, Floor, b + Listening::ScanGoodMs - 1000);
    CHECK(v.due(h, 0, l, false, b + Listening::ScanGoodMs));
    CHECK(v.passing());
    // A move during a burst ends it.
    h.heard(1, Floor + 12, Floor, b + Listening::ScanGoodMs + 1000);
    CHECK_FALSE(v.due(h, 1, l, false, b + Listening::ScanGoodMs + 1000));
    CHECK_FALSE(v.passing());
}

TEST_CASE("heard only below the fair level for as long as a level stays current: poor")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 8, Floor, 1000);
    h.heard(0, Floor, Floor, 30'000); // moved below the fair level
    CHECK(roaming::health(h, 0, l, 30'000) == Health::Fair);
    h.heard(0, Floor + 2, Floor, 90'000);
    CHECK(roaming::health(h, 0, l, 90'000) == Health::Fair);
    h.heard(0, Floor + 2, Floor, 1000 + HeardList::ValidMs);
    CHECK(roaming::health(h, 0, l, 1000 + HeardList::ValidMs) == Health::Poor);
    h.heard(0, Floor + 22, Floor, 181'000); // back at the fair level or above
    CHECK(roaming::health(h, 0, l, 181'000) == Health::Good);
    // Silent after fading while poor: lost.
    HeardList f;
    f.heard(0, Floor + 2, Floor, 1000);
    f.heard(0, Floor + 2, Floor, 61'000);
    f.heard(0, Floor + 2, Floor, 1000 + HeardList::ValidMs);
    REQUIRE(roaming::health(f, 0, l, 1000 + HeardList::ValidMs) == Health::Poor);
    CHECK(roaming::health(f, 0, l, 1000 + 2 * HeardList::ValidMs) == Health::Lost);
}

TEST_CASE("poor: to the strongest heard once, better by the margin")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 2, Floor, 1000);
    h.heard(0, Floor + 2, Floor, 61'000);
    const std::uint32_t t = 1000 + HeardList::ValidMs;
    h.heard(0, Floor + 2, Floor, t);
    REQUIRE(roaming::health(h, 0, l, t) == Health::Poor);
    h.heard(1, Floor + 5, Floor, t); // 3 dB better
    CHECK(moveTo(h, 0, Health::Poor, l, t) == -1);
    h.heard(2, Floor + 8, Floor, t); // 6 dB better, heard once
    CHECK(moveTo(h, 0, Health::Poor, l, t) == 2);
}

TEST_CASE("poor: passes in bursts, the gaps growing as while quiet")
{
    const Levels l;
    HeardList h;
    std::uint32_t last = 1000;
    h.heard(0, Floor + 1, Floor, last);
    // Heard below the fair level every 30 s, as from its beacons.
    auto hearUntil = [&](std::uint32_t t) {
        while (last + 30'000 <= t) {
            last += 30'000;
            h.heard(0, Floor + 1, Floor, last);
        }
    };
    Listening v;
    std::uint32_t t = 1000 + HeardList::ValidMs;
    hearUntil(t);
    REQUIRE(roaming::health(h, 0, l, t) == Health::Poor);
    CHECK(v.due(h, 0, l, false, t));
    CHECK(v.passing());
    t += Listening::BurstMs;
    std::uint32_t gap = Listening::FirstGapMs;
    for (int k = 0; k < 3; ++k) {
        hearUntil(t + gap - 1);
        CHECK_FALSE(v.due(h, 0, l, false, t + gap - 1));
        hearUntil(t + gap);
        REQUIRE(roaming::health(h, 0, l, t + gap) == Health::Poor);
        CHECK(v.due(h, 0, l, false, t + gap));
        t += gap + Listening::BurstMs;
        gap = std::min(gap * 2, Listening::MaxGapMs);
    }
    CHECK(gap == Listening::MaxGapMs);
}

TEST_CASE("bursts for display: none while strong, the next one while good or lost, now while one runs")
{
    const Levels l;
    std::uint32_t next = 0;
    HeardList e;
    e.heard(0, Floor + 22, Floor, 1000);
    Listening v;
    v.due(e, 0, l, false, 1000);
    CHECK(v.bursts(1000, next) == Listening::Bursts::None);
    HeardList g;
    g.heard(0, Floor + 12, Floor, 1000);
    v.due(g, 0, l, false, 1000);
    REQUIRE(v.bursts(1000, next) == Listening::Bursts::Later);
    CHECK(next == 1000 + Listening::ScanGoodMs);
    const std::uint32_t b = 1000 + Listening::ScanGoodMs;
    g.heard(0, Floor + 12, Floor, b - 1000);
    CHECK(v.due(g, 0, l, false, b));
    CHECK(v.bursts(b, next) == Listening::Bursts::Now);
    // Lost, not heard: bursts with gaps of FirstGapMs.
    HeardList f;
    f.heard(0, Floor + 3, Floor, 1000);
    Listening u;
    const std::uint32_t t0 = 1000 + HeardList::ValidMs;
    CHECK(u.due(f, 0, l, false, t0));
    CHECK(u.bursts(t0, next) == Listening::Bursts::Now);
    u.due(f, 0, l, false, t0 + Listening::BurstMs);
    REQUIRE(u.bursts(t0 + Listening::BurstMs, next) == Listening::Bursts::Later);
    CHECK(next == t0 + Listening::BurstMs + Listening::FirstGapMs);
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

TEST_CASE("lost with nothing at all heard for SilentMs: the gaps grow")
{
    const Levels l;
    HeardList h;
    h.heard(0, Floor + 3, Floor, 1000); // faded below the fair level, then silent: lost
    Listening v;
    const std::uint32_t t0 = 1000 + HeardList::ValidMs;
    REQUIRE(roaming::health(h, 0, l, t0) == Health::Lost);
    // The start of each burst, asked every second for two hours and a half.
    std::uint32_t starts[64] = {};
    std::size_t n = 0;
    bool was = false;
    for (std::uint32_t t = t0; t < t0 + 2 * Listening::SilentMs + Listening::SilentMs / 2 && n < 64; t += 1000) {
        const bool now = v.due(h, 0, l, false, t);
        if (now && !was) {
            starts[n++] = t;
        }
        was = now;
    }
    REQUIRE(n > 4);
    // Within the first SilentMs: a burst every BurstMs + FirstGapMs.
    CHECK(starts[1] - starts[0] == Listening::BurstMs + Listening::FirstGapMs);
    CHECK(starts[2] - starts[1] == Listening::BurstMs + Listening::FirstGapMs);
    // Later the gaps grow to MaxGapMs.
    CHECK(starts[n - 1] - starts[n - 2] == Listening::BurstMs + Listening::MaxGapMs);
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
