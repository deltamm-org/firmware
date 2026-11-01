// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "draw.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace dmm::app::ui {

using namespace services;

std::string_view cut(std::string_view s, int chars)
{
    return {s.data(), chars <= 0 ? 0 : std::min(s.size(), static_cast<std::size_t>(chars))};
}

void fillArea(Display &d, int x, int y, int w, int h, Color c)
{
    if (w > 0 && h > 0) {
        d.fillRect(x, y, w, h, c);
    }
}

void band(Display &d, int y, int h, std::initializer_list<Piece> pieces, Color ground, int x0, int x1)
{
    const int w = x1 > 0 ? x1 : d.width();
    Piece sorted[4];
    std::size_t n = 0;
    for (const Piece &pc : pieces) {
        if (!pc.text.empty() && n < std::size(sorted)) {
            sorted[n++] = pc;
        }
    }
    // Left to right; at most a few pieces.
    for (std::size_t i = 1; i < n; ++i) {
        for (std::size_t k = i; k > 0 && sorted[k].x < sorted[k - 1].x; --k) {
            std::swap(sorted[k], sorted[k - 1]);
        }
    }
    int cursor = x0;
    for (std::size_t i = 0; i < n; ++i) {
        const Piece &pc = sorted[i];
        const int x = std::max(pc.x, cursor);
        const int end = std::min(drawText(d, *pc.font, x, pc.y, pc.text, pc.ink, ground), w);
        fillArea(d, cursor, y, x - cursor, h, ground);
        fillArea(d, x, y, end - x, pc.y - y, ground);
        fillArea(d, x, pc.y + pc.font->height, end - x, y + h - pc.y - pc.font->height, ground);
        cursor = end;
    }
    fillArea(d, cursor, y, w - cursor, h, ground);
}

void line(Display &d, const Font &f, int x, int y, int w, std::string_view text, Color ink)
{
    const int end = drawText(d, f, x, y, text, ink, palette::Ground);
    if (end < x + w) {
        d.fillRect(end, y, x + w - end, f.height, palette::Ground);
    }
}

} // namespace dmm::app::ui
