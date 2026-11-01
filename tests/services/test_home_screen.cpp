// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/home_screen.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>

using dmm::services::Age;
using dmm::services::HomeLayout;
using dmm::services::HomePreset;
using dmm::services::HomeReadings;
namespace rows = dmm::services::home_rows;

TEST_CASE("the default layout is the Detailed preset")
{
    const HomeLayout l;
    CHECK(l.top == HomeLayout::Top::Talkgroup);
    CHECK(l.clock);
    CHECK(l.big == HomeLayout::Big::Channel);
    CHECK(l.middle == HomeLayout::Middle::Frequency);
    CHECK(l.beside == HomeLayout::Beside::Voltage);
    CHECK(l.bottom == HomeLayout::Bottom::Details);
    CHECK(dmm::services::presetOf(l) == HomePreset::Detailed);
    CHECK(dmm::services::presetLayout(HomePreset::Detailed) == l);
    CHECK(dmm::services::presetLayout(HomePreset::Own) == l);
}

TEST_CASE("presets set their rows and are recognised again")
{
    const HomeLayout simple = dmm::services::presetLayout(HomePreset::Simple);
    CHECK(simple.top == HomeLayout::Top::None);
    CHECK(simple.big == HomeLayout::Big::Contact);
    CHECK(simple.middle == HomeLayout::Middle::Talkgroup);
    CHECK(simple.bottom == HomeLayout::Bottom::LastCaller);
    CHECK(simple.clock);
    CHECK(dmm::services::presetOf(simple) == HomePreset::Simple);

    const HomeLayout roaming = dmm::services::presetLayout(HomePreset::Roaming);
    CHECK(roaming.middle == HomeLayout::Middle::Signal);
    CHECK(roaming.showsSignal());
    CHECK(dmm::services::presetOf(roaming) == HomePreset::Roaming);
}

TEST_CASE("a layout changed from a preset is the user's own")
{
    HomeLayout l = dmm::services::presetLayout(HomePreset::Simple);
    l.clock = false;
    CHECK(dmm::services::presetOf(l) == HomePreset::Own);
    l = {};
    l.beside = HomeLayout::Beside::Signal;
    CHECK(l.showsSignal());
    CHECK(dmm::services::presetOf(l) == HomePreset::Own);
    l.beside = HomeLayout::Beside::Voltage;
    CHECK_FALSE(l.showsSignal());
    CHECK(dmm::services::presetOf(l) == HomePreset::Detailed);
}

TEST_CASE("the signal meter lights a bar per 6 dB from -125 dBm to -65 dBm")
{
    CHECK(dmm::services::signalBars(-140) == 0);
    CHECK(dmm::services::signalBars(-125) == 0);
    CHECK(dmm::services::signalBars(-120) == 0);
    CHECK(dmm::services::signalBars(-119) == 1);
    CHECK(dmm::services::signalBars(-101) == 4);
    CHECK(dmm::services::signalBars(-71) == 9);
    CHECK(dmm::services::signalBars(-65) == 10);
    CHECK(dmm::services::signalBars(-42) == 10);
}

TEST_CASE("ages go by minutes, then by hours")
{
    CHECK(dmm::services::ageOf(0) == Age{});
    CHECK(dmm::services::ageOf(59) == Age{});
    CHECK(dmm::services::ageOf(60) == Age{Age::Unit::Minutes, 1});
    CHECK(dmm::services::ageOf(3599) == Age{Age::Unit::Minutes, 59});
    CHECK(dmm::services::ageOf(7300) == Age{Age::Unit::Hours, 2});
}

namespace {

std::uint8_t changed(const HomeLayout &l, const HomeReadings &before, const HomeReadings &after)
{
    return dmm::services::changedRows(l, before, after);
}

} // namespace

TEST_CASE("nothing is redrawn while the readings stay the same")
{
    HomeReadings r;
    r.clockMinutes = 1000;
    r.signalKnown = true;
    r.signalDbm = -90;
    r.centivolts = 780;
    r.volume = 8;
    for (const HomePreset p : {HomePreset::Detailed, HomePreset::Simple, HomePreset::Roaming}) {
        CHECK(changed(dmm::services::presetLayout(p), r, r) == 0);
    }
}

TEST_CASE("the clock is redrawn each minute, only while it is shown")
{
    HomeLayout l;
    HomeReadings a;
    HomeReadings b;
    a.clockMinutes = 600;
    b.clockMinutes = 601;
    CHECK(changed(l, a, b) == rows::Clock);
    a.clockMinutes = -1;
    CHECK(changed(l, a, b) == rows::Clock);
    l.clock = false;
    CHECK(changed(l, a, b) == 0);
}

TEST_CASE("a new signal level redraws only the rows that show it")
{
    HomeReadings a;
    HomeReadings b;
    a.signalKnown = b.signalKnown = true;
    a.signalDbm = -90;
    b.signalDbm = -91;

    HomeLayout l;
    CHECK(changed(l, a, b) == 0);
    l.middle = HomeLayout::Middle::Signal;
    CHECK(changed(l, a, b) == rows::Middle);
    l.beside = HomeLayout::Beside::Signal;
    CHECK(changed(l, a, b) == (rows::Middle | rows::Beside));
    l.middle = HomeLayout::Middle::Frequency;
    CHECK(changed(l, a, b) == rows::Beside);

    // Lost, and found again.
    b.signalKnown = false;
    CHECK(changed(l, a, b) == rows::Beside);
    CHECK(changed(l, b, a) == rows::Beside);
    // An unknown level is unknown whatever value it carries.
    a.signalKnown = false;
    CHECK(changed(l, a, b) == 0);
}

TEST_CASE("the voltage is redrawn when its shown digits or its colour change")
{
    HomeLayout l;
    HomeReadings a;
    HomeReadings b;
    a.centivolts = b.centivolts = 781;
    CHECK(changed(l, a, b) == 0);
    b.centivolts = 780;
    CHECK(changed(l, a, b) == rows::Beside);
    b.centivolts = 781;
    b.batteryLow = true;
    CHECK(changed(l, a, b) == rows::Beside);
    l.beside = HomeLayout::Beside::Volume;
    CHECK(changed(l, a, b) == 0);
}

TEST_CASE("the volume is redrawn when its step changes, only next to the power")
{
    HomeLayout l;
    HomeReadings a;
    HomeReadings b;
    a.volume = 3;
    b.volume = 4;
    CHECK(changed(l, a, b) == 0);
    l.beside = HomeLayout::Beside::Volume;
    CHECK(changed(l, a, b) == rows::Beside);
}

TEST_CASE("the last caller is redrawn for a new call and as the age moves on")
{
    HomeLayout l;
    l.bottom = HomeLayout::Bottom::LastCaller;
    HomeReadings a;
    HomeReadings b;
    CHECK(changed(l, a, b) == 0);
    b.lastCallAt = 5000;
    CHECK(changed(l, a, b) == rows::Bottom);
    a = b;
    b.lastCallAge = dmm::services::ageOf(30);
    CHECK(changed(l, a, b) == 0);
    b.lastCallAge = dmm::services::ageOf(61);
    CHECK(changed(l, a, b) == rows::Bottom);
    l.bottom = HomeLayout::Bottom::Details;
    CHECK(changed(l, a, b) == 0);
}

TEST_CASE("the position is redrawn when its shown digits change")
{
    HomeLayout l;
    l.bottom = HomeLayout::Bottom::Position;
    HomeReadings a;
    HomeReadings b;
    CHECK(changed(l, a, b) == 0);
    b.positionKnown = true;
    b.latitudeE5 = 6'012'345;
    b.longitudeE5 = 2'412'345;
    CHECK(changed(l, a, b) == rows::Bottom);
    a = b;
    b.longitudeE5 += 1;
    CHECK(changed(l, a, b) == rows::Bottom);
    b.positionKnown = false;
    CHECK(changed(l, a, b) == rows::Bottom);
}
