// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "menu_screen.h"

#include "draw.h"
#include "standby.h"

#include "dmm/lib/format.h"
#include "dmm/services/font.h"

#include <algorithm>
#include <utility>

namespace dmm::app::ui {

namespace {

using namespace services;
namespace p = palette;

const Font &Small = fonts::Spleen5x8;
const Font &Body = fonts::Spleen6x12;
const Font &Medium = fonts::Spleen8x16;
const Font &Large = fonts::Spleen12x24;

constexpr int Top = StandbyScreen::StatusH;
constexpr int RowH = 13;
constexpr int TitleH = RowH;
constexpr int ListTop = Top + TitleH;
// Room on the right for the scroll bar.
constexpr int ScrollW = 3;

static_assert(4 + static_cast<int>(menu::TitleChars) * 6 + 4 + 5 * 6 <= 160, "a title and the place fit");

// The title band: where the list is, and the place in it on the right.
void drawTitleBand(Display &d, std::string_view title, std::size_t index, std::size_t count)
{
    lib::StaticString<10> place;
    if (count > 1) {
        place << index + 1 << '/' << count;
    }
    band(d, Top, TitleH,
        {{4, Top + 1, cut(title, menu::TitleChars), &Body, p::Dim},
            {d.width() - 4 - textWidth(Body, place.view()), Top + 1, place.view(), &Body, p::Rule}},
        p::Panel);
}

void drawHintBar(Display &d, std::string_view hint)
{
    const int y = d.height() - StandbyScreen::HintH;
    d.fillRect(0, y, d.width(), 1, p::Rule);
    band(d, y + 1, StandbyScreen::HintH - 1, {{3, y + 2, hint, &Small, p::Dim}});
}

// A scroll bar at the right edge when @p count rows do not fit in @p shown;
// the track and the bar are painted side by side, never one over the other.
void drawScrollBar(Display &d, int top, int h, std::size_t first, int shown, std::size_t count)
{
    const int x = d.width() - ScrollW;
    if (count <= static_cast<std::size_t>(shown)) {
        d.fillRect(x, top, ScrollW, h, p::Ground);
        return;
    }
    const int barH = std::max(4, h * shown / static_cast<int>(count));
    const int barY = top + (h - barH) * static_cast<int>(first) / (static_cast<int>(count) - shown);
    d.fillRect(x, top, 1, h, p::Ground);
    fillArea(d, x + 1, top, 2, barY - top, p::Panel);
    d.fillRect(x + 1, barY, 2, barH, p::Dim);
    fillArea(d, x + 1, barY + barH, 2, top + h - barY - barH, p::Panel);
}

// A list row: the label on the left, the value right-aligned and cut to
// the room left, inverted when chosen.
void drawRow(Display &d, int y, const ListRow &row, bool on)
{
    const int right = d.width() - ScrollW;
    const Color fill = on ? p::Text : p::Ground;
    const std::string_view value = row.value.empty() && row.opens ? std::string_view(">") : row.value;
    const int room = std::max(0, (right - 10 - textWidth(Body, row.label)) / Body.width - 1);
    const std::string_view shown = cut(value, room);
    band(d, y, RowH,
        {{6, y + 1, row.label, &Body, on ? p::Ground : p::Text},
            {right - 4 - textWidth(Body, shown), y + 1, shown, &Body, on ? p::Ground : p::Dim}},
        fill, 0, right);
}

// Up to @p shown rows from ListTop, the scroll bar beside them; returns
// the y under them.
int drawRows(Display &d, std::span<const ListRow> rows, std::size_t first, std::size_t count, std::size_t cursor,
    int shown)
{
    for (int r = 0; r < shown; ++r) {
        const int y = ListTop + r * RowH;
        if (static_cast<std::size_t>(r) < rows.size()) {
            drawRow(d, y, rows[static_cast<std::size_t>(r)], first + static_cast<std::size_t>(r) == cursor);
        } else {
            d.fillRect(0, y, d.width() - ScrollW, RowH, p::Ground);
        }
    }
    const int end = ListTop + shown * RowH;
    drawScrollBar(d, ListTop, end - ListTop, first, shown, count);
    return end;
}

// Up to four choices as large buttons, the chosen one inverted and the one
// in use starred.
void drawButtons(Display &d, const menu::Choices &choices, std::size_t cursor, std::size_t inUse)
{
    const int w = d.width();
    const int bodyEnd = d.height() - StandbyScreen::HintH;
    constexpr int ButtonH = 20;
    constexpr int Pitch = 22;
    constexpr int Margin = 20;
    const int n = static_cast<int>(std::min<std::size_t>(choices.size(), 4));
    const int y0 = ListTop + (bodyEnd - ListTop - n * Pitch + (Pitch - ButtonH)) / 2;
    fillArea(d, 0, ListTop, w, y0 - ListTop, p::Ground);
    for (int i = 0; i < n; ++i) {
        const auto at = static_cast<std::size_t>(i);
        const int y = y0 + i * Pitch;
        const bool on = at == cursor;
        const Color fill = on ? p::Text : p::Panel;
        const std::string_view text = choices.text(at);
        const Font &f = textWidth(Medium, text) <= w - 3 * Margin ? Medium : Body;
        d.fillRect(0, y, Margin, ButtonH, p::Ground);
        band(d, y, ButtonH,
            {{(w - textWidth(f, text)) / 2, y + (ButtonH - f.height) / 2, text, &f, on ? p::Ground : p::Text},
                {w - 32, y + (ButtonH - Body.height) / 2, at == inUse ? "*" : "", &Body, on ? p::Ground : p::Dim}},
            fill, Margin, w - Margin);
        d.fillRect(w - Margin, y, Margin, ButtonH, p::Ground);
        fillArea(d, 0, y + ButtonH, w, (i + 1 < n ? Pitch : 0) - ButtonH, p::Ground);
    }
    const int end = y0 + (n - 1) * Pitch + ButtonH;
    fillArea(d, 0, end, w, bodyEnd - end, p::Ground);
}

// One choice of many, large between arrows, its place on a scale and the
// choice in use under it.
void drawSpinner(Display &d, const menu::Choices &choices, std::size_t cursor, std::size_t inUse)
{
    const int w = d.width();
    const int bodyEnd = d.height() - StandbyScreen::HintH;
    // The value, as large as it fits between the arrows.
    const int room = w - 2 * 22;
    std::string_view value = choices.text(cursor);
    const Font &f = textWidth(Large, value) <= room ? Large : textWidth(Medium, value) <= room ? Medium
                                                                                               : Body;
    value = cut(value, room / f.width);
    const int arrowY = ListTop + 10;
    band(d, ListTop, 40,
        {{6, arrowY, "<", &Large, p::Dim}, {(w - textWidth(f, value)) / 2, arrowY + (Large.height - f.height) / 2, value, &f, p::Text},
            {w - 6 - Large.width, arrowY, ">", &Large, p::Dim}});
    // The place: a step per choice while they fit, else a marker on a
    // track.
    const int scaleY = ListTop + 44;
    constexpr int ScaleH = 8;
    constexpr int Margin = 20;
    fillArea(d, 0, ListTop + 40, w, scaleY - ListTop - 40, p::Ground);
    const std::size_t n = std::max<std::size_t>(choices.size(), 1);
    d.fillRect(0, scaleY, Margin, ScaleH, p::Ground);
    if (n <= 16) {
        const int pitch = (w - 2 * Margin) / static_cast<int>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const int x = Margin + static_cast<int>(i) * pitch;
            d.fillRect(x, scaleY, pitch - 2, ScaleH, i <= cursor ? p::Text : p::Panel);
            d.fillRect(x + pitch - 2, scaleY, 2, ScaleH, p::Ground);
        }
        const int end = Margin + static_cast<int>(n) * pitch;
        fillArea(d, end, scaleY, w - end, ScaleH, p::Ground);
    } else {
        constexpr int MarkW = 6;
        const int trackW = w - 2 * Margin;
        const int mark = Margin + static_cast<int>(static_cast<std::size_t>(trackW - MarkW) * cursor / (n - 1));
        // Left of the marker and right of it: ground above and below a
        // two-pixel track.
        for (const auto &[x0, x1] : {std::pair{Margin, mark}, std::pair{mark + MarkW, Margin + trackW}}) {
            fillArea(d, x0, scaleY, x1 - x0, 3, p::Ground);
            fillArea(d, x0, scaleY + 3, x1 - x0, 2, p::Panel);
            fillArea(d, x0, scaleY + 5, x1 - x0, ScaleH - 5, p::Ground);
        }
        d.fillRect(mark, scaleY, MarkW, ScaleH, p::Text);
        d.fillRect(Margin + trackW, scaleY, w - Margin - trackW, ScaleH, p::Ground);
    }
    // The choice in use, then ground down to the hint bar.
    const int inUseY = scaleY + ScaleH + 8;
    fillArea(d, 0, scaleY + ScaleH, w, inUseY - scaleY - ScaleH, p::Ground);
    lib::StaticString<32> text;
    if (inUse < choices.size()) {
        text << "In use: " << choices.text(inUse);
    }
    band(d, inUseY, Body.height,
        {{(w - textWidth(Body, text.view())) / 2, inUseY, text.view(), &Body, inUse == cursor ? p::Rule : p::Dim}});
    fillArea(d, 0, inUseY + Body.height, w, bodyEnd - inUseY - Body.height, p::Ground);
}

// Choices with notes as a list, the note as the row's value.
void drawChoiceList(Display &d, const menu::Choices &choices, std::size_t cursor)
{
    ListRow rows[ListRows];
    const std::size_t first = firstShown(cursor, choices.size(), ListRows);
    std::size_t n = 0;
    for (; n < std::size(rows) && first + n < choices.size(); ++n) {
        rows[n] = {choices.text(first + n), choices.note(first + n)};
    }
    const int end = drawRows(d, std::span(rows, n), first, choices.size(), cursor, ListRows);
    fillArea(d, 0, end, d.width(), d.height() - StandbyScreen::HintH - end, p::Ground);
}

} // namespace

std::size_t firstShown(std::size_t cursor, std::size_t count, int rows)
{
    const auto shown = static_cast<std::size_t>(rows);
    if (count <= shown || cursor < shown / 2) {
        return 0;
    }
    return std::min(cursor - shown / 2, count - shown);
}

void drawListScreen(Display &d, std::string_view title, std::span<const ListRow> rows, std::size_t first,
    std::size_t count, std::size_t cursor, std::string_view hint)
{
    drawTitleBand(d, title, cursor, count);
    const int end = drawRows(d, rows, first, count, cursor, ListRows);
    fillArea(d, 0, end, d.width(), d.height() - StandbyScreen::HintH - end, p::Ground);
    drawHintBar(d, hint);
}

void drawMenuScreen(Display &d, std::string_view title, std::span<const ListRow> rows, std::size_t first,
    std::size_t count, std::size_t cursor, std::string_view about1, std::string_view about2, std::string_view hint)
{
    const int w = d.width();
    drawTitleBand(d, title, cursor, count);
    const int panelTop = drawRows(d, rows, first, count, cursor, MenuRows);
    const int panelH = d.height() - StandbyScreen::HintH - panelTop;
    d.fillRect(0, panelTop, w, 1, p::Ground);
    band(d, panelTop + 1, RowH, {{4, panelTop + 2, about1, &Body, p::Dim}}, p::Panel);
    band(d, panelTop + 1 + RowH, panelH - 1 - RowH, {{4, panelTop + 1 + RowH, about2, &Body, p::Dim}}, p::Panel);
    drawHintBar(d, hint);
}

void drawValueScreen(Display &d, std::string_view title, const menu::Choices &choices, std::size_t cursor,
    std::size_t inUse, std::string_view hint)
{
    const menu::Style style = menu::styleOf(choices);
    // The buttons show every choice: the place in the list says nothing.
    drawTitleBand(d, title, cursor, style == menu::Style::Buttons ? 0 : choices.size());
    switch (style) {
    case menu::Style::Buttons:
        drawButtons(d, choices, cursor, inUse);
        break;
    case menu::Style::Spinner:
        drawSpinner(d, choices, cursor, inUse);
        break;
    case menu::Style::List:
        drawChoiceList(d, choices, cursor);
        break;
    }
    drawHintBar(d, hint);
}

} // namespace dmm::app::ui
