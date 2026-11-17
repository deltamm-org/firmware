// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/dedicated_mode.h"

#include <doctest/doctest.h>

using dmm::services::DedicatedMode;
using dmm::services::DedicatedModeState;
using dmm::services::Key;
using dmm::services::ReceiverLock;
using Change = DedicatedModeState::Change;
using Use = DedicatedModeState::KeyUse;

namespace {

constexpr ReceiverLock Aprs{144'800'000, true};

} // namespace

TEST_CASE("a dedicated mode turns on at its frequency and off again")
{
    DedicatedModeState s;
    CHECK_FALSE(s.on());
    CHECK(s.turnOn(DedicatedMode::Aprs, Aprs) == Change::TurnedOn);
    CHECK(s.on());
    CHECK(s.mode() == DedicatedMode::Aprs);
    CHECK(s.lock() == Aprs);
    CHECK(s.turnOn(DedicatedMode::Aprs, Aprs) == Change::Unchanged);
    CHECK(s.turnOff() == Change::TurnedOff);
    CHECK_FALSE(s.on());
    CHECK(s.mode() == DedicatedMode::Off);
    CHECK(s.turnOff() == Change::Unchanged);
}

TEST_CASE("a dedicated mode without a frequency stays off")
{
    DedicatedModeState s;
    CHECK(s.turnOn(DedicatedMode::Aprs, ReceiverLock{0, true}) == Change::NoFrequency);
    CHECK_FALSE(s.on());
    // One that is on keeps its lock.
    s.turnOn(DedicatedMode::Aprs, Aprs);
    CHECK(s.turnOn(DedicatedMode::Aprs, ReceiverLock{0, true}) == Change::NoFrequency);
    CHECK(s.lock() == Aprs);
}

TEST_CASE("a new frequency locks the receiver anew; turning on Off turns off")
{
    DedicatedModeState s;
    s.turnOn(DedicatedMode::Aprs, Aprs);
    CHECK(s.turnOn(DedicatedMode::Aprs, ReceiverLock{144'800'000, false}) == Change::Moved);
    CHECK_FALSE(s.lock().wide);
    CHECK(s.turnOn(DedicatedMode::Off, Aprs) == Change::TurnedOff);
    CHECK_FALSE(s.on());
}

TEST_CASE("every key works as usual while no mode is on")
{
    DedicatedModeState s;
    for (unsigned k = 0; k < static_cast<unsigned>(Key::Count); ++k) {
        CHECK(s.keyUse(static_cast<Key>(k)) == Use::Usual);
    }
    CHECK(s.knobUse() == Use::Usual);
}

TEST_CASE("in APRS mode the channel keys are refused and PTT sends a beacon")
{
    DedicatedModeState s;
    s.turnOn(DedicatedMode::Aprs, Aprs);
    CHECK(s.knobUse() == Use::List);
    CHECK(s.keyUse(Key::Up) == Use::List);
    CHECK(s.keyUse(Key::Down) == Use::List);
    CHECK(s.keyUse(Key::Ptt) == Use::Action);
    CHECK(s.keyUse(Key::Front1) == Use::Refused);
    CHECK(s.keyUse(Key::Hash) == Use::Refused);
    CHECK(s.keyUse(Key::Digit0) == Use::Refused);
    CHECK(s.keyUse(Key::Digit9) == Use::Refused);
    CHECK(s.keyUse(Key::Menu) == Use::Usual);
    CHECK(s.keyUse(Key::Exit) == Use::Usual);
    CHECK(s.keyUse(Key::Front2) == Use::List);
    CHECK(s.keyUse(Key::Side1) == Use::Usual);
    CHECK(s.keyUse(Key::Star) == Use::Usual);
    s.turnOff();
    CHECK(s.keyUse(Key::Ptt) == Use::Usual);
    CHECK(s.keyUse(Key::Front1) == Use::Usual);
}

TEST_CASE("modes have names")
{
    CHECK(std::string_view(dmm::services::modeName(DedicatedMode::Aprs)) == "APRS");
    CHECK(std::string_view(dmm::services::modeName(DedicatedMode::Off)).empty());
}
