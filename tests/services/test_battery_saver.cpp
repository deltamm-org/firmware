// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/battery_saver.h"

#include <doctest/doctest.h>

using dmm::services::BatterySaver;
using Action = BatterySaver::Action;

TEST_CASE("battery save starts after five idle seconds and cycles sleep and listen")
{
    BatterySaver s;
    s.setMode(1);
    CHECK(s.update(1000, 4999, true) == Action::None);
    CHECK_FALSE(s.active());

    CHECK(s.update(2000, 5000, true) == Action::Sleep);
    CHECK(s.asleep());
    CHECK(s.nextChangeMs() == 2150);
    CHECK(s.update(2149, 5149, true) == Action::None);
    CHECK(s.update(2150, 5150, true) == Action::Wake);
    CHECK_FALSE(s.asleep());
    CHECK(s.nextChangeMs() == 2330);
    CHECK(s.update(2329, 5329, true) == Action::None);
    CHECK(s.update(2330, 5330, true) == Action::Sleep);
    CHECK(s.cycles() == 2);
}

TEST_CASE("mode 2 sleeps 70 ms; out-of-range modes count as 1")
{
    BatterySaver s;
    s.setMode(2);
    CHECK(s.update(0, 6000, true) == Action::Sleep);
    CHECK(s.nextChangeMs() == 70);
    s.setMode(9);
    CHECK(s.mode() == 1);
}

TEST_CASE("activity, a busy radio or a stopped receiver ends battery save")
{
    BatterySaver s;
    s.setMode(1);
    REQUIRE(s.update(0, 6000, true) == Action::Sleep);
    CHECK(s.update(50, 0, true) == Action::Wake); // a key
    CHECK_FALSE(s.active());
    CHECK(s.update(60, 10, true) == Action::None);

    REQUIRE(s.update(5000, 5000, true) == Action::Sleep);
    CHECK(s.update(5010, 5010, false) == Action::None); // retuned or transmitting
    CHECK_FALSE(s.active());
    CHECK_FALSE(s.asleep());

    REQUIRE(s.update(9000, 9000, true) == Action::Sleep);
    s.setMode(0);
    CHECK(s.update(9010, 9010, true) == Action::Wake);
    CHECK(s.update(9020, 9020, true) == Action::None);
}

TEST_CASE("after a reset the next idle update starts a new cycle")
{
    BatterySaver s;
    s.setMode(1);
    REQUIRE(s.update(0, 6000, true) == Action::Sleep);
    s.reset();
    CHECK_FALSE(s.asleep());
    CHECK(s.update(10, 6010, true) == Action::Sleep);
}

TEST_CASE("battery save copes with the millisecond counter wrapping")
{
    BatterySaver s;
    s.setMode(1);
    REQUIRE(s.update(0xFFFF'FFF0u, 6000, true) == Action::Sleep);
    CHECK(s.update(0x0000'0010u, 6032, true) == Action::None);
    CHECK(s.update(0x0000'0086u, 6150, true) == Action::Wake);
}
