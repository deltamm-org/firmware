// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "aprs_screens.h"

#include "draw.h"

#include "dmm/services/aprs_home.h"
#include "dmm/services/font.h"
#include "dmm/services/nmea.h"

#include <iterator>
#include <string_view>

namespace dmm::app::ui {

namespace {

using services::Color;
using services::Display;
using services::Font;
using services::latitudeText;
using services::longitudeText;
namespace fonts = services::fonts;
namespace p = palette;

const Font &Small = fonts::Spleen5x8;
const Font &Body = fonts::Spleen6x12;
const Font &Medium = fonts::Spleen8x16;
const Font &Large = fonts::Spleen12x24;

// The home screen's rows, as the mode's screen shares them.
constexpr int LabelY = StandbyScreen::StatusH;
constexpr int NameY = LabelY + 19;
constexpr int PanelY = NameY + 28;
constexpr int SettingsY = PanelY + 22;
constexpr int InfoY = SettingsY + 18;
constexpr int RightX = 96;

} // namespace

lib::StaticString<10> distanceText(const GpsState &gps, const aprs::HeardStation &s)
{
    if (!gps.fix.valid || !s.hasPosition) {
        return {};
    }
    return services::aprs::distanceText(
        services::aprs::pathBetween(gps.fix.latitudeE7, gps.fix.longitudeE7, s.latitudeE7, s.longitudeE7).metres);
}

lib::StaticString<8> ageText(std::uint32_t thenMs, std::uint32_t nowMs)
{
    return services::aprs::ageText((nowMs - thenMs) / 1000);
}

void drawModeScreen(Display &d, const StandbyState &s, const ModeScreen &m)
{
    const int w = d.width();
    const int h = d.height();
    const std::string_view clock = s.layout.clock ? s.clock : std::string_view{};
    band(d, LabelY, NameY - LabelY, {{4, LabelY + 4, m.top, &Small, p::Beacon}}, p::Ground, 0, RightX);
    band(d, LabelY, NameY - LabelY, {{w - 4 - textWidth(Medium, clock), LabelY + 2, clock, &Medium, p::Text}},
        p::Ground, RightX);

    const int nameH = PanelY - NameY - 1;
    const Font &bf = textWidth(Large, m.big) <= w - 8 ? Large : Medium;
    band(d, NameY, nameH, {{4, NameY + (Large.height - bf.height) / 2 + 1, m.big, &bf, p::Text}});
    d.fillRect(0, PanelY - 1, w, 1, p::Rule);

    band(d, PanelY, SettingsY - PanelY,
        {{4, PanelY + 5, m.panel, &Body, p::Text},
            {w - 4 - textWidth(Body, m.panelRight), PanelY + 5, m.panelRight, &Body, p::Dim}},
        p::Panel);

    band(d, SettingsY, InfoY - SettingsY - 1,
        {{4, SettingsY + 5, m.leftLabel, &Small, p::Dim}, {26, SettingsY + 3, m.left, &Body, p::Text}}, p::Ground, 0,
        RightX);
    band(d, SettingsY, InfoY - SettingsY - 1,
        {{RightX, SettingsY + 5, m.rightLabel, &Small, p::Dim},
            {RightX + 22, SettingsY + 3, m.right, &Body, m.rightWarn ? p::Warn : p::Text}},
        p::Ground, RightX);
    d.fillRect(0, InfoY - 1, w, 1, p::Rule);

    band(d, InfoY, h - StandbyScreen::HintH - InfoY, {{4, InfoY + 5, m.bottom, &Small, p::Dim}});

    d.fillRect(0, h - StandbyScreen::HintH, w, 1, p::Rule);
    band(d, h - StandbyScreen::HintH + 1, StandbyScreen::HintH - 1,
        {{3, h - StandbyScreen::HintH + 2, m.keys, &Small, p::Dim}});
}

void drawAprsStation(Display &d, const aprs::HeardStation &s, const GpsState &gps, std::uint32_t nowMs)
{
    const int w = d.width();
    const int h = d.height();
    const int top = StandbyScreen::StatusH;
    constexpr const char *Labels[] = {"Lat", "Lon", "Dist", "Alt", "Speed", "Text"};
    const int firstRow = top + Medium.height + 5;
    d.fillRect(0, top, w, h - top, p::Ground);
    drawText(d, Medium, 4, top + 2, s.call, p::Text, p::Ground);
    const lib::StaticString<8> age = ageText(s.heardAtMs, nowMs);
    drawText(d, Body, w - 4 - textWidth(Body, age.view()), top + 4, age.view(), p::Dim, p::Ground);
    for (int i = 0; i < static_cast<int>(std::size(Labels)); ++i) {
        drawText(d, Body, 4, firstRow + i * (Body.height + 1), Labels[i], p::Dim, p::Ground);
    }
    d.fillRect(0, h - StandbyScreen::HintH, w, 1, p::Rule);

    int y = firstRow;
    auto value = [&](std::string_view text) {
        line(d, Body, 46, y, w - 50, text, p::Text);
        y += Body.height + 1;
    };
    value(s.hasPosition ? latitudeText(s.latitudeE7).view() : "-");
    value(s.hasPosition ? longitudeText(s.longitudeE7).view() : "-");
    lib::StaticString<24> v;
    if (gps.fix.valid && s.hasPosition) {
        const services::aprs::Path path =
            services::aprs::pathBetween(gps.fix.latitudeE7, gps.fix.longitudeE7, s.latitudeE7, s.longitudeE7);
        v << distanceText(gps, s).view() << ' ' << static_cast<int>(path.bearingDeg + 0.5f) % 360 << ' '
          << services::aprs::compassPoint(path.bearingDeg);
    } else {
        v << "-";
    }
    value(v.view());
    v.clear();
    if (s.hasAltitude) {
        v << s.altitudeM << " m";
    } else {
        v << "-";
    }
    value(v.view());
    v.clear();
    if (s.hasCourse) {
        // 1 knot = 1.852 km/h.
        v << (s.speedKnots * 1852 + 500) / 1000 << " km/h " << s.courseDeg << " deg";
    } else {
        v << "-";
    }
    value(v.view());
    value(s.text[0] != '\0' ? std::string_view(s.text) : "-");

    v.clear();
    v << (s.via[0] != '\0' ? "via " : "direct") << s.via << ", " << s.packets << (s.packets == 1 ? " packet" : " packets");
    line(d, Small, 3, h - StandbyScreen::HintH + 2, w - 6, v.view(), p::Dim);
}

} // namespace dmm::app::ui
