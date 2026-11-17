// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "aprs.h"
#include "standby.h"

#include "dmm/lib/format.h"
#include "dmm/services/display.h"

#include <cstdint>

/// The APRS screens, below the status bar the home screen draws.
namespace dmm::app::ui {

/// "12.3 km" from our fix to the station, or empty without both positions.
lib::StaticString<10> distanceText(const GpsState &gps, const aprs::HeardStation &s);
/// "45 s", "12 min", "3 h": how long ago @p thenMs was.
lib::StaticString<8> ageText(std::uint32_t thenMs, std::uint32_t nowMs);

/// A dedicated receive mode's home screen: the home screen's rows with
/// the mode's texts.
struct ModeScreen
{
    /// The top line, left of the clock: the mode and its frequency.
    std::string_view top;
    std::string_view big;
    /// On the panel: on the left, and right-aligned.
    std::string_view panel;
    std::string_view panelRight;
    /// Under the panel, a label and a value on each half; the right value
    /// in amber when @c rightWarn.
    std::string_view leftLabel;
    std::string_view left;
    std::string_view rightLabel;
    std::string_view right;
    bool rightWarn = false;
    std::string_view bottom;
    /// The hint bar.
    std::string_view keys;
};

/// The mode's home screen in place of the channel's, below the status bar,
/// drawn over what is there; the clock as @p s has it.
void drawModeScreen(services::Display &d, const StandbyState &s, const ModeScreen &m);

/// One heard station: its call and age, position, distance and bearing
/// from our fix, altitude, speed and course, its text, and the path it
/// came by.
void drawAprsStation(services::Display &d, const aprs::HeardStation &s, const GpsState &gps, std::uint32_t nowMs);

} // namespace dmm::app::ui
