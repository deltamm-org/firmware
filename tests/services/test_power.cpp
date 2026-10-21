// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/power.h"

#include <doctest/doctest.h>

using namespace dmm;
namespace power = dmm::services::power;
using power::AwakeReason;

TEST_CASE("stay-awake reasons combine and idle time starts at the last release")
{
    power::hold(AwakeReason::Carrier);
    power::hold(AwakeReason::Display);
    CHECK(power::isHeld(AwakeReason::Carrier));
    CHECK(power::heldMask() == 0b1000010);
    CHECK(power::idleForMs() == 0);

    power::release(AwakeReason::Carrier);
    CHECK(power::idleForMs() == 0);
    power::release(AwakeReason::Display);
    os::sleepMs(30);
    CHECK(power::heldMask() == 0);
    CHECK(power::idleForMs() >= 25);

    power::release(AwakeReason::Display);
    CHECK(power::heldMask() == 0);
    CHECK(power::name(AwakeReason::UsbLink) == "usb");
}

TEST_CASE("activity starts the idle time again")
{
    os::sleepMs(30);
    REQUIRE(power::idleForMs() >= 25);
    power::activity();
    CHECK(power::idleForMs() < 25);
}
