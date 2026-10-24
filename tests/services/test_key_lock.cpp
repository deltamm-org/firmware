// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/key_lock.h"

#include <doctest/doctest.h>

using dmm::services::Key;
using dmm::services::KeyLock;
using dmm::services::KeyLockSettings;
using Type = dmm::services::KeyDebouncer::EventType;
using V = KeyLock::Verdict;

namespace {

KeyLock::Context standby(std::uint32_t now = 0)
{
    return {true, false, false, now};
}

KeyLock::Context menuScreen(std::uint32_t now = 0)
{
    return {false, false, false, now};
}

// A full press of @p key: press, then release; returns the press verdict.
V tap(KeyLock &lock, Key key, const KeyLock::Context &c)
{
    const V v = lock.key(key, Type::Press, c);
    CHECK(lock.key(key, Type::Release, c) == V::Pass);
    return v;
}

void lockByLongStar(KeyLock &lock, std::uint32_t now = 0)
{
    CHECK(lock.key(Key::Star, Type::Press, standby(now)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::LongPress, standby(now + 1000)) == V::Locked);
    CHECK(lock.key(Key::Star, Type::Release, standby(now + 1200)) == V::Pass);
    CHECK(lock.locked());
}

} // namespace

TEST_CASE("long * locks; a short * does its normal job")
{
    KeyLock lock;
    CHECK(tap(lock, Key::Star, standby()) == V::Pass);
    CHECK_FALSE(lock.locked());
    lockByLongStar(lock);
}

TEST_CASE("locked: each category refuses its keys and nothing else")
{
    KeyLock lock;
    lockByLongStar(lock);
    for (Key k : {Key::Digit0, Key::Digit9, Key::Star, Key::Hash, Key::Exit, Key::Up, Key::Down, Key::Front1, Key::Front2}) {
        CHECK(tap(lock, k, standby(5000)) == V::Refused);
    }
    for (Key k : {Key::Side1, Key::Side2, Key::Side3}) {
        CHECK(tap(lock, k, standby(5000)) == V::Refused);
    }
    CHECK(lock.knob(standby(5000)) == V::Refused);
    CHECK(lock.ptt(true, standby(5000)) == V::Refused);

    // Only the keypad locked.
    KeyLockSettings keypadOnly;
    keypadOnly.knob = false;
    keypadOnly.side = false;
    lock.setSettings(keypadOnly);
    CHECK(tap(lock, Key::Digit5, standby(5000)) == V::Refused);
    CHECK(tap(lock, Key::Side1, standby(5000)) == V::Pass);
    CHECK(lock.knob(standby(5000)) == V::Pass);
    CHECK(lock.ptt(true, standby(5000)) == V::Pass);

    // Repeats and long presses of locked keys are refused too.
    lock.setSettings(KeyLockSettings{});
    CHECK(lock.key(Key::Up, Type::Repeat, standby(6000)) == V::Refused);
    CHECK(lock.key(Key::Exit, Type::LongPress, standby(6000)) == V::Refused);
}

TEST_CASE("functions that must always work pass, and so does PTT release")
{
    KeyLock lock;
    lockByLongStar(lock);
    KeyLock::Context monitorKey = standby(2000);
    monitorKey.exemptFunction = true;
    CHECK(tap(lock, Key::Side2, monitorKey) == V::Pass);
    CHECK(tap(lock, Key::Front1, monitorKey) == V::Pass);
    CHECK(lock.ptt(false, standby(2000)) == V::Pass);
}

TEST_CASE("Menu then * unlocks; a late * or another key in between does not")
{
    KeyLock lock;
    lockByLongStar(lock);
    CHECK(tap(lock, Key::Menu, standby(2000)) == V::UnlockPrompt);
    CHECK(tap(lock, Key::Star, standby(2000 + KeyLock::UnlockWindowMs)) == V::Refused); // too late
    CHECK(lock.locked());

    CHECK(tap(lock, Key::Menu, standby(9000)) == V::UnlockPrompt);
    CHECK(tap(lock, Key::Digit1, standby(9100)) == V::Refused);
    CHECK(tap(lock, Key::Star, standby(9200)) == V::Refused); // the prompt was closed
    CHECK(lock.locked());

    CHECK(tap(lock, Key::Menu, standby(12000)) == V::UnlockPrompt);
    CHECK(tap(lock, Key::Star, standby(12500)) == V::Unlocked);
    CHECK_FALSE(lock.locked());
    CHECK(tap(lock, Key::Digit1, standby(13000)) == V::Pass);
}

TEST_CASE("holding * after unlocking does not lock again")
{
    KeyLock lock;
    lockByLongStar(lock);
    CHECK(tap(lock, Key::Menu, standby(2000)) == V::UnlockPrompt);
    CHECK(lock.key(Key::Star, Type::Press, standby(2500)) == V::Unlocked);
    CHECK(lock.key(Key::Star, Type::LongPress, standby(3500)) == V::Ignored); // quietly
    CHECK(lock.key(Key::Star, Type::Release, standby(3600)) == V::Pass);
    CHECK_FALSE(lock.locked());
    lockByLongStar(lock, 5000); // a new long press still locks
}

TEST_CASE("screens that do not take the lock work normally while locked")
{
    KeyLock lock;
    lockByLongStar(lock);
    for (Key k : {Key::Menu, Key::Exit, Key::Up, Key::Down, Key::Digit3, Key::Side1, Key::Front1, Key::Star}) {
        CHECK(tap(lock, k, menuScreen(2000)) == V::Pass);
    }
    CHECK(lock.knob(menuScreen(2000)) == V::Pass);
    CHECK(lock.ptt(true, menuScreen(2000)) == V::Pass);
    CHECK(lock.locked()); // still locked for when standby returns
    // Long * there neither locks nor unlocks.
    CHECK(lock.key(Key::Star, Type::LongPress, menuScreen(2000)) == V::Pass);
    CHECK(lock.locked());
}

TEST_CASE("nothing is locked while transmitting")
{
    KeyLock lock;
    lockByLongStar(lock);
    KeyLock::Context tx = standby(2000);
    tx.transmitting = true;
    CHECK(tap(lock, Key::Digit1, tx) == V::Pass);
    CHECK(lock.knob(tx) == V::Pass);
    // And a long * while transmitting does not lock.
    KeyLock open;
    CHECK(open.key(Key::Star, Type::LongPress, tx) == V::Pass);
    CHECK_FALSE(open.locked());
}

TEST_CASE("forced lock: always on, no unlock, other screens still work")
{
    KeyLock lock;
    KeyLockSettings forced;
    forced.forced = true;
    lock.setSettings(forced);
    CHECK_FALSE(lock.locked());
    CHECK(tap(lock, Key::Digit1, standby()) == V::Refused);
    CHECK(tap(lock, Key::Menu, standby()) == V::Refused);
    CHECK(tap(lock, Key::Star, standby()) == V::Refused);
    CHECK(tap(lock, Key::Digit1, menuScreen()) == V::Pass);
}

TEST_CASE("auto lock after the idle time, only on a lockable screen and not while transmitting")
{
    KeyLock lock;
    KeyLockSettings s;
    s.autoLockMs = 10'000;
    lock.setSettings(s);
    CHECK(tap(lock, Key::Digit1, standby(0)) == V::Pass);
    CHECK_FALSE(lock.update(standby(9'999)));
    CHECK_FALSE(lock.update(menuScreen(20'000))); // in a menu: waits
    KeyLock::Context tx = standby(20'000);
    tx.transmitting = true;
    CHECK_FALSE(lock.update(tx)); // transmitting restarts the wait
    CHECK_FALSE(lock.update(standby(29'999)));
    CHECK(lock.update(standby(30'000)));
    CHECK(lock.locked());
    CHECK_FALSE(lock.update(standby(60'000))); // only once

    // Off by default.
    KeyLock never;
    CHECK_FALSE(never.update(standby(1'000'000)));
}

TEST_CASE("Menu then a long * never locks; a plain long * still does")
{
    KeyLock lock;
    CHECK(tap(lock, Key::Menu, standby(1000)) == V::Pass); // unlocked: Menu is just Menu
    CHECK(lock.key(Key::Star, Type::Press, standby(1500)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::LongPress, standby(2500)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::Release, standby(2700)) == V::Pass);
    CHECK_FALSE(lock.locked());
    // Long after Menu, a long * locks as usual.
    lockByLongStar(lock, 10'000);
}

TEST_CASE("the Menu window counts to the * press, not to its long press")
{
    KeyLock lock;
    // * pressed 2 s after Menu: its long press comes 3 s after Menu, and
    // still does not lock.
    CHECK(tap(lock, Key::Menu, standby(1000)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::Press, standby(3000)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::LongPress, standby(4000)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::Release, standby(4200)) == V::Pass);
    CHECK_FALSE(lock.locked());
    // * pressed after the window: locks.
    CHECK(tap(lock, Key::Menu, standby(10'000)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::Press, standby(13'000)) == V::Pass);
    CHECK(lock.key(Key::Star, Type::LongPress, standby(14'000)) == V::Locked);
}

TEST_CASE("turning auto lock on starts its idle time afresh")
{
    KeyLock lock;
    CHECK(tap(lock, Key::Digit1, standby(0)) == V::Pass);
    CHECK_FALSE(lock.update(standby(60'000))); // auto lock off
    KeyLockSettings s;
    s.autoLockMs = 10'000;
    lock.setSettings(s);
    CHECK_FALSE(lock.update(standby(60'001))); // idle a minute already, but just switched on
    CHECK_FALSE(lock.update(standby(70'000)));
    CHECK(lock.update(standby(70'001)));
}
