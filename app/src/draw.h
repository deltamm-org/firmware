// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/display.h"
#include "dmm/services/font.h"

#include <initializer_list>
#include <string_view>

/// The colours and drawing helpers the radio's own screens share.
namespace dmm::app::ui {

/// The palette of the radio's own screens: black ground, white primary
/// text, grey secondary text, thin grey rules between areas, and colour
/// only for state.
namespace palette {
inline constexpr services::Color Ground = services::rgb(0, 0, 0);
inline constexpr services::Color Text = services::rgb(255, 255, 255);
inline constexpr services::Color Dim = services::rgb(150, 150, 150);
inline constexpr services::Color Rule = services::rgb(70, 70, 70);
inline constexpr services::Color Transmit = services::rgb(210, 40, 30);
inline constexpr services::Color Receive = services::rgb(40, 170, 60);
inline constexpr services::Color Warn = services::rgb(230, 160, 0);
/// A band set off from the ground: titles, panels, buttons.
inline constexpr services::Color Panel = services::rgb(28, 28, 32);
} // namespace palette

inline int textWidth(const services::Font &f, std::string_view s)
{
    return static_cast<int>(s.size()) * f.width;
}

/// At most @p chars characters of @p s.
std::string_view cut(std::string_view s, int chars);

/// A filled rectangle; nothing when it is empty.
void fillArea(services::Display &d, int x, int y, int w, int h, services::Color c);

/// One text of a band: where it goes, in which font and colour.
struct Piece
{
    int x;
    int y;
    std::string_view text;
    const services::Font *font;
    services::Color ink;
};

/// A band of the screen, rows [y, y + h) and columns [x0, x1) (to the
/// right edge when x1 is 0): each piece of text is drawn with its own
/// background first, then every other pixel of the band is painted
/// @p ground. Nothing is cleared before it is drawn over, so a redraw does
/// not flicker, and text of any size fits as long as it lies inside the
/// band. At most four pieces, which must not overlap side by side.
void band(services::Display &d, int y, int h, std::initializer_list<Piece> pieces, services::Color ground = palette::Ground,
    int x0 = 0, int x1 = 0);

/// @p text from @p x, the rest of a @p w wide row painted ground.
void line(services::Display &d, const services::Font &f, int x, int y, int w, std::string_view text, services::Color ink);

} // namespace dmm::app::ui
