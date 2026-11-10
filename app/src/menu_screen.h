// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "menu.h"

#include "dmm/services/display.h"

#include <cstddef>
#include <span>
#include <string_view>

/// The menu's screens and the lists that look like it, under the status
/// bar: a title band with where the list is and the place in it, rows,
/// and a hint bar naming the keys. Everything is drawn in place, each
/// pixel painted once per update, so nothing blanks before it is redrawn.
namespace dmm::app::ui {

/// One row of a list: a label, its value or empty, and whether Menu opens
/// something from it (a ">" where it has no value).
struct ListRow
{
    std::string_view label;
    std::string_view value;
    bool opens = false;
};

/// Rows a list screen shows at once, and a menu screen above its panel.
inline constexpr int ListRows = 7;
inline constexpr int MenuRows = 5;

/// The first of @p rows rows shown when row @p cursor of @p count is chosen.
std::size_t firstShown(std::size_t cursor, std::size_t count, int rows);

/// A list: @p rows are the ones shown, from row @p first of @p count, with
/// row @p cursor chosen.
void drawListScreen(services::Display &d, std::string_view title, std::span<const ListRow> rows, std::size_t first,
    std::size_t count, std::size_t cursor, std::string_view hint);

/// The menu: as a list of MenuRows rows, with a panel of two lines about
/// the chosen row under them.
void drawMenuScreen(services::Display &d, std::string_view title, std::span<const ListRow> rows, std::size_t first,
    std::size_t count, std::size_t cursor, std::string_view about1, std::string_view about2, std::string_view hint);

/// A value screen in the style the choices ask for (menu::Style), the
/// choice at @p cursor chosen and the one at @p inUse marked as in use.
void drawValueScreen(services::Display &d, std::string_view title, const menu::Choices &choices, std::size_t cursor,
    std::size_t inUse, std::string_view hint);

} // namespace dmm::app::ui
