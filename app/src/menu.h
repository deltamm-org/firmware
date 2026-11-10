// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/format.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/input.h"
#include "dmm/services/key_lock.h"
#include "dmm/services/power_level.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// The menu as data: its groups and rows, the values a setting can take,
/// and where the user is in it. Nothing here draws or changes the radio;
/// the ui task draws the menu from this and applies what is chosen.
///
/// A new setting is a value of Item, a line in the item table (label,
/// about text, kind), a row in a group's list, its choices and its place
/// in Settings; a new group is a value of Group, its list of rows, a line
/// in the group table, and a row for it in the group it opens from.
namespace dmm::app::ui::menu {

/// The menu's rows other than groups: settings, screens and information.
enum class Item : std::uint8_t
{
    Squelch,
    SquelchMode,
    Power,
    RoamStatus, // opens the roaming status screen
    Roaming,
    RoamZone,
    GpsStatus, // opens the GPS screen
    Gps,
    Backlight,
    BacklightTime,
    KeyLock,
    AutoLock,
    BatterySave,
    TimeZone,
    GpsSystems,
    Log,     // opens the log
    KeyTest, // opens the key test
    Version,
    Count,
};

/// The groups: what is used while operating the radio first, then
/// Settings and Tools, whose groups open from their rows.
enum class Group : std::uint8_t
{
    Top,
    Channel,
    Roaming,
    Gps,
    Display,
    Settings,
    Tools,
    Keys,
    Clock,
    GpsSettings,
    Count,
};

/// What Menu does on an item's row.
enum class Kind : std::uint8_t
{
    Value,  // opens a screen to choose the value
    Screen, // opens a screen of its own
    Info,   // nothing; the about text says it all
};

/// One row of a group: an item, or a group that opens from it.
struct Row
{
    constexpr Row(Item i)
        : item(i)
    {
    }
    constexpr Row(Group g)
        : group(g)
    {
    }
    bool opensGroup() const { return group != Group::Count; }

    Item item = Item::Count;
    Group group = Group::Count;
};

struct GroupInfo
{
    std::string_view name;
    /// What the group holds, shown while its row is chosen.
    std::string_view about;
    std::span<const Row> rows;
};

struct ItemInfo
{
    std::string_view label;
    /// What the row does, shown while it is chosen.
    std::string_view about;
    Kind kind;
};

const GroupInfo &info(Group group);
const ItemInfo &info(Item item);
/// The row's name and about text, whether item or group.
std::string_view label(Row row);
std::string_view about(Row row);

/// The group that @p group opens from, and the row there that opens it.
/// The top has none (Group::Count).
struct Place
{
    Group group;
    std::size_t row;
};
Place parentOf(Group group);

/// How much text the screen shows, in characters: titles, row labels, and
/// the two lines of about text.
inline constexpr std::size_t TitleChars = 18;
inline constexpr std::size_t LabelChars = 14;
inline constexpr std::size_t AboutChars = 25;

using Title = lib::StaticString<TitleChars>;
/// A group's title: the group it opens from too, where both fit.
Title groupTitle(Group group);
/// A value screen's title: the group too, where both fit.
Title valueTitle(Group group, Item item);

/// @p text in two lines of at most AboutChars, broken at a space; false
/// when it does not fit.
bool splitAbout(std::string_view text, std::string_view &first, std::string_view &second);

/// The values the menu shows and changes, as the radio uses them.
struct Settings
{
    std::uint8_t squelch = 3; // 0 = open, 1..5
    services::SquelchMode squelchMode = services::SquelchMode::Carrier;
    services::PowerLevel power = services::PowerLevel::Low;
    bool roaming = true;
    std::uint8_t roamZone = services::RoamingSettings::NoZone;
    std::uint8_t backlight = 4; // 1..5
    std::uint16_t backlightS = 0;
    services::KeyLockSettings keyLock;
    std::uint8_t batterySave = 0;
    bool gps = false;
    std::uint8_t gpsSystems = 3;
    std::int16_t timeZoneMinutes = 0;
};

/// Where a setting is kept: the general settings, the channel in use, or
/// nowhere.
enum class Kept : std::uint8_t
{
    Settings,
    Channel,
    No,
};
Kept keptIn(Item item);

/// The value of @p item's setting in @p s, as its choices number it.
std::int32_t choiceOf(Item item, const Settings &s);
/// Sets @p item's setting in @p s to @p value.
void setChoice(Item item, std::int32_t value, Settings &s);

using ValueText = lib::StaticString<16>;
/// @p value of @p item as a row shows it.
ValueText valueText(Item item, std::int32_t value);

/// The values one item can take, each with its text and an optional note.
class Choices
{
public:
    static constexpr std::size_t MaxChoices = services::CodeplugSource::MaxRoamingZones;
    static constexpr std::size_t TextBytes = 17;
    static constexpr std::size_t NoteBytes = 9;

    void clear() { m_count = 0; }
    /// Cut to fit: keep @p text to 16 characters and @p note to 8.
    void add(std::int32_t value, std::string_view text, std::string_view note = {});
    std::size_t size() const { return m_count; }
    std::int32_t value(std::size_t i) const { return m_value[i]; }
    std::string_view text(std::size_t i) const { return m_text[i]; }
    std::string_view note(std::size_t i) const { return m_note[i]; }
    bool hasNotes() const;
    /// The place of @p value, or size() when it is not one of them.
    std::size_t find(std::int32_t value) const;

private:
    std::int32_t m_value[MaxChoices] = {};
    char m_text[MaxChoices][TextBytes] = {};
    char m_note[MaxChoices][NoteBytes] = {};
    std::size_t m_count = 0;
};

/// Lists @p item's choices into @p out; false for an item whose choices
/// come from the codeplug (the roaming zones), which the caller lists.
bool listChoices(Item item, Choices &out);

/// How a value screen shows its choices: up to four as large buttons, more
/// as one large value with its place on a scale, or a list where they
/// carry notes.
enum class Style : std::uint8_t
{
    Buttons,
    Spinner,
    List,
};
Style styleOf(const Choices &choices);

/// Where the user is in the menu, and what each key does there.
class Navigator
{
public:
    /// What a key or the knob did.
    enum class Step : std::uint8_t
    {
        None,
        Moved,  // the cursor moved
        Opened, // another list is up
        Choose, // a value screen is to open: list the choices, then choose()
        Set,    // a value was chosen: chosen()
        Show,   // the row's item opens a screen of its own
        Left,   // the menu is closed
    };

    /// Opens @p group at the row last chosen there. Opened @p quick, from
    /// the standby screen, Exit and a value set close the menu.
    void open(Group group = Group::Top, bool quick = false);
    bool isOpen() const { return m_open; }

    Group group() const { return m_group; }
    const GroupInfo &groupInfo() const { return info(m_group); }
    std::size_t row() const { return m_rows[static_cast<std::size_t>(m_group)]; }
    Row current() const { return groupInfo().rows[row()]; }

    /// A value screen is open for current().item.
    bool choosing() const { return m_choosing; }
    Choices &choices() { return m_choices; }
    const Choices &choices() const { return m_choices; }
    /// After Step::Choose, with choices() listed: opens the value screen on
    /// @p inUse, or on the first choice when it is none of them.
    void choose(std::int32_t inUse);
    std::size_t cursor() const { return m_cursor; }
    std::size_t inUse() const { return m_inUse; }
    std::int32_t chosen() const { return m_choices.value(m_cursor); }
    Style style() const { return styleOf(m_choices); }

    /// Up, Down, Menu or Exit, pressed or repeating.
    Step press(services::Key key);
    /// The knob, clockwise positive.
    Step turn(int steps);

    /// The key hint for the screen up.
    std::string_view hint() const;

private:
    Step move(int steps);
    Step select();
    Step back();

    bool m_open = false;
    bool m_quick = false;
    bool m_choosing = false;
    Group m_group = Group::Top;
    // The row last chosen in each group.
    std::uint8_t m_rows[static_cast<std::size_t>(Group::Count)] = {};
    Choices m_choices;
    std::size_t m_cursor = 0;
    std::size_t m_inUse = 0;
};

} // namespace dmm::app::ui::menu
