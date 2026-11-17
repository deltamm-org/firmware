// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/aprs_home.h"

#include <doctest/doctest.h>

#include <string_view>

namespace aprs = dmm::services::aprs;
using State = aprs::BeaconStatus::State;

namespace {

// Our position and a station 0.1 degrees north-east of it.
aprs::HomeInput heardOne()
{
    aprs::HomeInput in;
    in.frequencyHz = 144'800'000;
    in.call = "N0CALL-7";
    in.hasPosition = true;
    in.latitudeE7 = 515780000;
    in.longitudeE7 = 1000000;
    in.text = "Made-up car";
    in.heardAgoS = 125;
    in.fix = true;
    in.ourLatitudeE7 = 514780000;
    in.ourLongitudeE7 = 0;
    in.stations = 3;
    in.packets = 17;
    in.decoding = true;
    in.beacon = {State::Next, 252};
    return in;
}

} // namespace

TEST_CASE("the APRS home screen names the frequency and the station heard last")
{
    const aprs::HomeRows r = aprs::homeRows(heardOne());
    CHECK(r.top.view() == "APRS 144.800");
    CHECK(r.big.view() == "N0CALL-7");
    // 11.1 km north and 6.9 km east: 13.09 km, shown cut to tenths.
    CHECK(r.panel.view() == "13.0 km NE");
    CHECK(r.panelRight.view() == "2 min");
    CHECK(r.stations.view() == "3");
    CHECK(r.packets.view() == "17");
    CHECK_FALSE(r.decoderIdle);
    CHECK(r.bottom.view() == "Next beacon 4:12");
}

TEST_CASE("the APRS home screen before any station is heard")
{
    aprs::HomeInput in;
    in.frequencyHz = 144'390'000;
    in.beacon = {State::Off, 0};
    const aprs::HomeRows r = aprs::homeRows(in);
    CHECK(r.top.view() == "APRS 144.390");
    CHECK(r.big.view() == "Listening");
    CHECK(r.panel.view() == "Nothing heard yet");
    CHECK(r.panelRight.view().empty());
    CHECK(r.stations.view() == "0");
    CHECK(r.decoderIdle);
    CHECK(r.bottom.view() == "Beacons off");
}

TEST_CASE("without both positions the panel shows the station's text, cut to fit")
{
    aprs::HomeInput in = heardOne();
    in.fix = false;
    in.text = "A rather long comment that goes on";
    aprs::HomeRows r = aprs::homeRows(in);
    // "2 min" on the right leaves 19 characters; cut at the last space.
    CHECK(r.panel.view() == "A rather long");
    CHECK(r.bottom.view() == "Next beacon 4:12  no GPS");
    in.heardAgoS = 5;
    CHECK(aprs::homeRows(in).panel.view() == "A rather long comment");
    // No space in the second half: cut in the word.
    in.text = "Abcdefghijklmnopqrstuvwxyz";
    r = aprs::homeRows(in);
    CHECK(r.panel.view() == "Abcdefghijklmnopqrstu");
    CHECK(r.panel.size() + 1 + r.panelRight.size() == aprs::HomeRows::PanelChars);
    in.fix = true;
    in.hasPosition = false;
    in.text = "Status";
    CHECK(aprs::homeRows(in).panel.view() == "Status");
}

TEST_CASE("a station without a text says what is missing")
{
    aprs::HomeInput in = heardOne();
    in.text = "";
    in.fix = false;
    CHECK(aprs::homeRows(in).panel.view() == "Our GPS: no fix");
    in.hasPosition = false;
    CHECK(aprs::homeRows(in).panel.view() == "No position");
    in.fix = true;
    CHECK(aprs::homeRows(in).panel.view() == "No position");
}

TEST_CASE("the APRS home screen tells our beacon's state")
{
    aprs::HomeInput in = heardOne();
    in.beacon = {State::NoStation, 0};
    CHECK(aprs::homeRows(in).bottom.view() == "No APRS callsign");
    in.beacon = {State::WaitingForFix, 0};
    CHECK(aprs::homeRows(in).bottom.view() == "Beacon waits for GPS");
    in.beacon = {State::Queued, 0};
    CHECK(aprs::homeRows(in).bottom.view() == "Beacon queued");
    in.beacon = {State::Next, 3605};
    CHECK(aprs::homeRows(in).bottom.view() == "Next beacon 60:05");
}

TEST_CASE("distances, bearings and ages read as on the heard list")
{
    CHECK(aprs::distanceText(850.0F).view() == "850 m");
    CHECK(aprs::distanceText(12'345.0F).view() == "12.3 km");
    CHECK(aprs::distanceText(250'400.0F).view() == "250 km");
    CHECK(std::string_view(aprs::compassPoint(0.0F)) == "N");
    CHECK(std::string_view(aprs::compassPoint(350.0F)) == "N");
    CHECK(std::string_view(aprs::compassPoint(100.0F)) == "E");
    CHECK(std::string_view(aprs::compassPoint(225.0F)) == "SW");
    CHECK(aprs::ageText(45).view() == "45 s");
    CHECK(aprs::ageText(720).view() == "12 min");
    CHECK(aprs::ageText(3 * 3600 + 5).view() == "3 h");
    // Due east across the date line: the short way.
    const aprs::Path p = aprs::pathBetween(0, 1'799'000'000, 0, -1'799'000'000);
    CHECK(p.metres == doctest::Approx(22'239.0).epsilon(0.01));
    CHECK(p.bearingDeg == doctest::Approx(90.0).epsilon(0.01));
}
