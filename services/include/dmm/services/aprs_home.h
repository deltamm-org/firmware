// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/format.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dmm::services::aprs {

/// Distance and bearing from one position to another.
struct Path
{
    float metres;
    float bearingDeg;
};

/// From the first position to the second, on a flat map around them:
/// within a few metres per kilometre at the ranges radio reaches.
Path pathBetween(std::int32_t lat1E7, std::int32_t lon1E7, std::int32_t lat2E7, std::int32_t lon2E7);

/// "850 m", "12.3 km", "250 km".
lib::StaticString<10> distanceText(float metres);

/// "N", "NE", ..., "NW" for a bearing in degrees.
const char *compassPoint(float degrees);

/// "45 s", "12 min", "3 h".
lib::StaticString<8> ageText(std::uint32_t seconds);

/// Our own beacon, as the home screen tells it.
struct BeaconStatus
{
    enum class State : std::uint8_t
    {
        NoStation,     // the codeplug has no APRS callsign
        Off,           // no automatic beacons
        Next,          // the next one in nextInS
        WaitingForFix, // one waits for a GPS position
        Queued,        // one is on its way out
    };
    State state = State::Off;
    std::uint32_t nextInS = 0;
};

/// What the APRS mode's home screen is made from.
struct HomeInput
{
    std::uint32_t frequencyHz = 0;
    /// The station heard last (empty call: none yet): its position if it
    /// sent one, its comment or status, and how long ago.
    std::string_view call;
    bool hasPosition = false;
    std::int32_t latitudeE7 = 0;
    std::int32_t longitudeE7 = 0;
    std::string_view text;
    std::uint32_t heardAgoS = 0;
    /// Our GPS position, if there is a fix.
    bool fix = false;
    std::int32_t ourLatitudeE7 = 0;
    std::int32_t ourLongitudeE7 = 0;
    /// Stations in the heard list, and packets heard.
    std::size_t stations = 0;
    std::uint32_t packets = 0;
    /// The audio is being decoded now.
    bool decoding = false;
    BeaconStatus beacon;
};

/// The home screen's rows as text.
struct HomeRows
{
    /// Characters across the panel: its text, a space and the age on the
    /// right. A longer text is cut, at a space where one is near the end.
    static constexpr std::size_t PanelChars = 25;

    lib::StaticString<16> top;       // "APRS 144.800"
    lib::StaticString<12> big;       // the callsign, or "Listening"
    lib::StaticString<28> panel;     // "12.3 km NE", or the station's text
    lib::StaticString<8> panelRight; // "2 min"
    lib::StaticString<8> stations;   // "5"
    lib::StaticString<12> packets;   // "23"
    bool decoderIdle = false;        // packets shown in amber
    lib::StaticString<32> bottom;    // our beacon
};

HomeRows homeRows(const HomeInput &in);

} // namespace dmm::services::aprs
