// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/aprs_home.h"

#include <cmath>

namespace dmm::services::aprs {

namespace {

constexpr float Pi = 3.14159265F;

// Seconds as "4:12".
lib::StaticString<12> minutesText(std::uint32_t seconds)
{
    lib::StaticString<12> t;
    t << seconds / 60 << ':' << lib::zeroPadded(seconds % 60, 2);
    return t;
}

// @p text in at most @p room characters: cut at the last space in its
// second half, else in the middle of a word.
std::string_view fitted(std::string_view text, std::size_t room)
{
    if (text.size() <= room) {
        return text;
    }
    const bool wordEnds = text[room] == ' ';
    text.remove_suffix(text.size() - room);
    const std::size_t space = text.rfind(' ');
    if (!wordEnds && space != std::string_view::npos && space >= room / 2) {
        text.remove_suffix(text.size() - space);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

Path pathBetween(std::int32_t lat1E7, std::int32_t lon1E7, std::int32_t lat2E7, std::int32_t lon2E7)
{
    constexpr float MetresPerE7 = 6'371'000.0F * Pi / 180.0F / 1e7F;
    const float meanLat = (static_cast<float>(lat1E7) + static_cast<float>(lat2E7)) / 2.0F * (Pi / 180.0F / 1e7F);
    const float north = static_cast<float>(lat2E7 - lat1E7) * MetresPerE7;
    // Across the date line the short way round.
    std::int64_t dLon = static_cast<std::int64_t>(lon2E7) - lon1E7;
    if (dLon > 1'800'000'000) {
        dLon -= 3'600'000'000;
    } else if (dLon < -1'800'000'000) {
        dLon += 3'600'000'000;
    }
    const float east = static_cast<float>(dLon) * MetresPerE7 * std::cos(meanLat);
    float bearing = std::atan2(east, north) * (180.0F / Pi);
    if (bearing < 0) {
        bearing += 360;
    }
    return {std::sqrt(north * north + east * east), bearing};
}

lib::StaticString<10> distanceText(float metres)
{
    lib::StaticString<10> t;
    if (metres < 1000) {
        t << static_cast<int>(metres) << " m";
    } else if (metres < 100'000) {
        const int tenths = static_cast<int>(metres / 100);
        t << tenths / 10 << '.' << tenths % 10 << " km";
    } else {
        t << static_cast<int>(metres / 1000) << " km";
    }
    return t;
}

const char *compassPoint(float degrees)
{
    constexpr const char *Points[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return Points[static_cast<int>((degrees + 22.5F) / 45.0F) % 8];
}

lib::StaticString<8> ageText(std::uint32_t seconds)
{
    lib::StaticString<8> t;
    if (seconds < 60) {
        t << seconds << " s";
    } else if (seconds < 3600) {
        t << seconds / 60 << " min";
    } else {
        t << seconds / 3600 << " h";
    }
    return t;
}

HomeRows homeRows(const HomeInput &in)
{
    HomeRows r;
    r.top << "APRS " << in.frequencyHz / 1'000'000 << '.' << lib::zeroPadded(in.frequencyHz / 1000 % 1000, 3);

    if (in.call.empty()) {
        r.big << "Listening";
        r.panel << "Nothing heard yet";
    } else {
        r.big << in.call;
        r.panelRight << ageText(in.heardAgoS).view();
        const std::size_t room = HomeRows::PanelChars - 1 - r.panelRight.size();
        if (in.hasPosition && in.fix) {
            const Path p = pathBetween(in.ourLatitudeE7, in.ourLongitudeE7, in.latitudeE7, in.longitudeE7);
            r.panel << distanceText(p.metres).view() << ' ' << compassPoint(p.bearingDeg);
        } else if (!in.text.empty()) {
            r.panel << fitted(in.text, room);
        } else {
            r.panel << (in.hasPosition ? "Our GPS: no fix" : "No position");
        }
    }

    r.stations << in.stations;
    r.packets << in.packets;
    r.decoderIdle = !in.decoding;

    using State = BeaconStatus::State;
    switch (in.beacon.state) {
    case State::NoStation:
        r.bottom << "No APRS callsign";
        break;
    case State::Off:
        r.bottom << "Beacons off";
        break;
    case State::Next:
        r.bottom << "Next beacon " << minutesText(in.beacon.nextInS).view();
        if (!in.fix) {
            r.bottom << "  no GPS";
        }
        break;
    case State::WaitingForFix:
        r.bottom << "Beacon waits for GPS";
        break;
    case State::Queued:
        r.bottom << "Beacon queued";
        break;
    }
    return r;
}

} // namespace dmm::services::aprs
