// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "menu.h"

#include <doctest/doctest.h>

#include <optional>
#include <string_view>

using namespace dmm::app::ui::menu;
using dmm::services::Key;
using Step = Navigator::Step;

namespace {

constexpr std::size_t Groups = static_cast<std::size_t>(Group::Count);
constexpr std::size_t Items = static_cast<std::size_t>(Item::Count);

Group groupAt(std::size_t g)
{
    return static_cast<Group>(g);
}

Item itemAt(std::size_t i)
{
    return static_cast<Item>(i);
}

// Moves the cursor of the open list onto @p row.
void moveTo(Navigator &nav, std::size_t row)
{
    nav.turn(static_cast<int>(row) - static_cast<int>(nav.row()));
}

// The navigator in a fresh state, each value screen listed from the model.
Step chooseHere(Navigator &nav, const Settings &s)
{
    const Step step = nav.press(Key::Menu);
    if (step == Step::Choose) {
        const Item item = nav.current().item;
        listChoices(item, nav.choices());
        nav.choose(choiceOf(item, s));
    }
    return step;
}

} // namespace

TEST_CASE("every group but the top opens from exactly one row, and every item is on one")
{
    std::size_t itemRows[Items] = {};
    std::size_t groupRows[Groups] = {};
    for (std::size_t g = 0; g < Groups; ++g) {
        for (const Row row : info(groupAt(g)).rows) {
            if (row.opensGroup()) {
                ++groupRows[static_cast<std::size_t>(row.group)];
            } else {
                ++itemRows[static_cast<std::size_t>(row.item)];
            }
        }
    }
    CHECK(groupRows[static_cast<std::size_t>(Group::Top)] == 0);
    for (std::size_t g = 1; g < Groups; ++g) {
        CAPTURE(info(groupAt(g)).name);
        CHECK(groupRows[g] == 1);
        // The way up ends at the top.
        Group up = groupAt(g);
        for (int depth = 0; depth < 4 && up != Group::Top; ++depth) {
            up = parentOf(up).group;
        }
        CHECK(up == Group::Top);
    }
    for (std::size_t i = 0; i < Items; ++i) {
        CAPTURE(info(itemAt(i)).label);
        CHECK(itemRows[i] == 1);
    }
    CHECK(parentOf(Group::Top).group == Group::Count);
}

TEST_CASE("every row has an about text that fits its two lines")
{
    for (std::size_t g = 0; g < Groups; ++g) {
        for (const Row row : info(groupAt(g)).rows) {
            CAPTURE(label(row));
            std::string_view first;
            std::string_view second;
            CHECK(!about(row).empty());
            CHECK(splitAbout(about(row), first, second));
            CHECK(first.size() <= AboutChars);
            CHECK(second.size() <= AboutChars);
        }
    }
}

TEST_CASE("labels and titles fit the screen without being cut")
{
    for (std::size_t i = 0; i < Items; ++i) {
        CAPTURE(info(itemAt(i)).label);
        CHECK(info(itemAt(i)).label.size() <= LabelChars);
    }
    for (std::size_t g = 0; g < Groups; ++g) {
        const GroupInfo &group = info(groupAt(g));
        CAPTURE(group.name);
        CHECK(group.name.size() <= LabelChars);
        const Title title = groupTitle(groupAt(g));
        CHECK(!title.truncated());
        CHECK(title.view().ends_with(group.name));
        for (const Row row : group.rows) {
            if (row.opensGroup()) {
                continue;
            }
            const Title t = valueTitle(groupAt(g), row.item);
            CAPTURE(t.view());
            CHECK(!t.truncated());
            CHECK(t.view().ends_with(info(row.item).label));
        }
    }
    CHECK(groupTitle(Group::Channel).view() == "Menu > Channel");
    CHECK(groupTitle(Group::Display).view() == "Menu > Display");
    CHECK(groupTitle(Group::Clock).view() == "Settings > Clock");
    CHECK(groupTitle(Group::Keys).view() == "Keys & battery");
    CHECK(valueTitle(Group::Channel, Item::Squelch).view() == "Channel > Squelch");
    CHECK(valueTitle(Group::Channel, Item::SquelchMode).view() == "Squelch type");
}

TEST_CASE("Menu opens a group and Exit goes back one level onto the row that opened it")
{
    Navigator nav;
    nav.open();
    CHECK(nav.isOpen());
    CHECK(nav.group() == Group::Top);
    CHECK(nav.row() == 0);

    // Settings > Keys & battery.
    moveTo(nav, 4);
    CHECK(nav.current().group == Group::Settings);
    CHECK(nav.press(Key::Menu) == Step::Opened);
    CHECK(nav.group() == Group::Settings);
    CHECK(nav.current().group == Group::Home);
    moveTo(nav, 2);
    CHECK(nav.press(Key::Menu) == Step::Opened);
    CHECK(nav.group() == Group::Keys);
    CHECK(nav.press(Key::Down) == Step::Moved);
    CHECK(nav.current().item == Item::AutoLock);

    CHECK(nav.press(Key::Exit) == Step::Opened);
    CHECK(nav.group() == Group::Settings);
    CHECK(nav.current().group == Group::Keys);
    CHECK(nav.press(Key::Exit) == Step::Opened);
    CHECK(nav.group() == Group::Top);
    CHECK(nav.current().group == Group::Settings);
    CHECK(nav.press(Key::Exit) == Step::Left);
    CHECK(!nav.isOpen());
    CHECK(nav.press(Key::Down) == Step::None);

    // Opened again: where it was, each group on its row.
    nav.open();
    CHECK(nav.current().group == Group::Settings);
    nav.press(Key::Menu);
    nav.press(Key::Menu);
    CHECK(nav.current().item == Item::AutoLock);
}

TEST_CASE("Display opens from the top of the menu")
{
    Navigator nav;
    nav.open();
    moveTo(nav, 3);
    CHECK(nav.current().group == Group::Display);
    CHECK(nav.press(Key::Menu) == Step::Opened);
    CHECK(nav.current().item == Item::Backlight);
    CHECK(nav.press(Key::Exit) == Step::Opened);
    CHECK(nav.group() == Group::Top);
    CHECK(nav.current().group == Group::Display);
}

TEST_CASE("the cursor wraps round both ends of a list")
{
    Navigator nav;
    nav.open();
    const std::size_t rows = info(Group::Top).rows.size();
    CHECK(nav.press(Key::Up) == Step::Moved);
    CHECK(nav.row() == rows - 1);
    CHECK(nav.turn(1) == Step::Moved);
    CHECK(nav.row() == 0);
    CHECK(nav.turn(0) == Step::None);
}

TEST_CASE("a value is chosen with Menu and left alone with Exit")
{
    Settings s;
    s.power = dmm::services::PowerLevel::High;
    Navigator nav;
    nav.open(Group::Channel);
    moveTo(nav, 2);
    REQUIRE(nav.current().item == Item::Power);
    CHECK(nav.hint() == "Menu: change   Exit: back");

    CHECK(chooseHere(nav, s) == Step::Choose);
    CHECK(nav.choosing());
    CHECK(nav.style() == Style::Buttons);
    CHECK(nav.cursor() == 2);
    CHECK(nav.inUse() == 2);
    CHECK(nav.hint() == "Menu: set   Exit: back");
    // Buttons move as drawn: Down is the next one.
    CHECK(nav.press(Key::Down) == Step::Moved);
    CHECK(nav.chosen() == 3);
    CHECK(nav.press(Key::Exit) == Step::Opened);
    CHECK(!nav.choosing());
    CHECK(nav.current().item == Item::Power);

    chooseHere(nav, s);
    nav.press(Key::Up);
    CHECK(nav.press(Key::Menu) == Step::Set);
    CHECK(nav.chosen() == 1);
    CHECK(!nav.choosing());
    CHECK(nav.isOpen());
    CHECK(nav.current().item == Item::Power);
    setChoice(Item::Power, nav.chosen(), s);
    CHECK(s.power == dmm::services::PowerLevel::Mid);
}

TEST_CASE("on a large value Up raises it, and a value with notes is a list")
{
    Settings s;
    s.squelch = 3;
    Navigator nav;
    nav.open(Group::Channel);
    chooseHere(nav, s);
    CHECK(nav.style() == Style::Spinner);
    CHECK(nav.chosen() == 3);
    nav.press(Key::Up);
    CHECK(nav.chosen() == 4);
    nav.press(Key::Down);
    nav.press(Key::Down);
    CHECK(nav.chosen() == 2);
    CHECK(nav.inUse() == 3);

    Choices c;
    listChoices(Item::BatterySave, c);
    CHECK(styleOf(c) == Style::List);
    listChoices(Item::TimeZone, c);
    CHECK(styleOf(c) == Style::Spinner);
    CHECK(c.size() == 53);
}

TEST_CASE("opened from the standby screen, Exit and a value set close the menu")
{
    Settings s;
    Navigator nav;
    nav.open(Group::Channel, true);
    CHECK(nav.press(Key::Exit) == Step::Left);
    CHECK(!nav.isOpen());

    nav.open(Group::Channel, true);
    chooseHere(nav, s);
    CHECK(nav.press(Key::Menu) == Step::Set);
    CHECK(!nav.isOpen());
}

TEST_CASE("screens open from their rows and the version row does nothing")
{
    Navigator nav;
    nav.open(Group::Gps);
    REQUIRE(nav.current().item == Item::GpsStatus);
    CHECK(nav.hint() == "Menu: open   Exit: back");
    CHECK(nav.press(Key::Menu) == Step::Show);
    nav.open(Group::Tools);
    moveTo(nav, 2);
    REQUIRE(nav.current().item == Item::Version);
    CHECK(nav.press(Key::Menu) == Step::None);
    CHECK(nav.hint() == "Exit: back");
}

TEST_CASE("every choice of every setting is set, read back and kept")
{
    for (std::size_t i = 0; i < Items; ++i) {
        const Item item = itemAt(i);
        CAPTURE(info(item).label);
        if (info(item).kind != Kind::Value) {
            CHECK(keptIn(item) == Kept::No);
            continue;
        }
        CHECK(keptIn(item) != Kept::No);
        Choices choices;
        if (!listChoices(item, choices)) {
            // Listed from the codeplug.
            CHECK(item == Item::RoamZone);
            Settings s;
            setChoice(item, 7, s);
            CHECK(choiceOf(item, s) == 7);
            continue;
        }
        REQUIRE(choices.size() > 0);
        for (std::size_t c = 0; c < choices.size(); ++c) {
            Settings s;
            setChoice(item, choices.value(c), s);
            CHECK(choiceOf(item, s) == choices.value(c));
            CHECK(!valueText(item, choices.value(c)).view().empty());
            CHECK(choices.find(choices.value(c)) == c);
        }
    }
}

TEST_CASE("the lock covers each set of keys and the time zone reads as an offset")
{
    Settings s;
    setChoice(Item::KeyLock, 2, s);
    CHECK(s.keyLock.keypad);
    CHECK(!s.keyLock.side);
    CHECK(!s.keyLock.knob);
    CHECK(valueText(Item::KeyLock, choiceOf(Item::KeyLock, s)).view() == "Keypad");
    CHECK(valueText(Item::TimeZone, 150).view() == "UTC+2:30");
    CHECK(valueText(Item::TimeZone, -540).view() == "UTC-9:00");
    CHECK(valueText(Item::TimeZone, 0).view() == "UTC");
    CHECK(valueText(Item::BacklightTime, 120).view() == "2 min");
    CHECK(valueText(Item::AutoLock, 5).view() == "5 s");
}

TEST_CASE("an about text too long for two lines does not fit")
{
    std::string_view first;
    std::string_view second;
    CHECK(splitAbout("short", first, second));
    CHECK(first == "short");
    CHECK(second.empty());
    CHECK(splitAbout("Open, or 1 to 5: higher needs a stronger signal", first, second));
    CHECK(first == "Open, or 1 to 5: higher");
    CHECK(second == "needs a stronger signal");
    CHECK(!splitAbout("This sentence is much too long to fit in the two lines of the panel", first, second));
}

TEST_CASE("the home screen group sets a preset or each row, kept with the preferences")
{
    using dmm::services::HomeLayout;
    using dmm::services::HomePreset;
    CHECK(parentOf(Group::Home).group == Group::Settings);
    CHECK(parentOf(Group::Home).row == 0);
    CHECK(keptIn(Item::HomeBottom) == Kept::Preferences);

    Settings s;
    CHECK(valueText(Item::HomePreset, choiceOf(Item::HomePreset, s)).view() == "Detailed");
    setChoice(Item::HomePreset, static_cast<std::int32_t>(HomePreset::Simple), s);
    CHECK(s.home == dmm::services::presetLayout(HomePreset::Simple));
    CHECK(valueText(Item::HomeMiddle, choiceOf(Item::HomeMiddle, s)).view() == "Talkgroup");

    // A row changed on its own makes the layout the user's own.
    setChoice(Item::HomeBeside, static_cast<std::int32_t>(HomeLayout::Beside::Signal), s);
    CHECK(s.home.beside == HomeLayout::Beside::Signal);
    CHECK(valueText(Item::HomePreset, choiceOf(Item::HomePreset, s)).view() == "Own");
    // Own is shown but not offered: the value screen opens on the first preset.
    Choices c;
    REQUIRE(listChoices(Item::HomePreset, c));
    CHECK(c.size() == 3);
    CHECK(c.find(choiceOf(Item::HomePreset, s)) == c.size());
}

TEST_CASE("the microphone gains follow the codeplug's level until set in dB, FM to a lower top")
{
    Settings s;
    s.plugDmrMicGainDb = 8;
    s.plugFmMicGainDb = 20;
    CHECK(keptIn(Item::DmrMicGain) == Kept::Preferences);
    CHECK(choiceOf(Item::DmrMicGain, s) == FromCodeplug + 8);
    CHECK(valueText(Item::DmrMicGain, choiceOf(Item::DmrMicGain, s)).view() == "Plug +8 dB");

    Choices c;
    REQUIRE(listChoices(Item::DmrMicGain, c, s));
    CHECK(c.size() == 32);
    CHECK(c.value(0) == FromCodeplug + 8);
    CHECK(c.text(0) == "Codeplug +8 dB");
    CHECK(c.text(1) == "+0 dB");
    CHECK(c.value(31) == 30);
    CHECK(styleOf(c) == Style::Spinner);
    REQUIRE(listChoices(Item::FmMicGain, c, s));
    CHECK(c.size() == 26);
    CHECK(c.text(0) == "Codeplug +20 dB");
    CHECK(c.value(25) == 24);

    setChoice(Item::FmMicGain, 12, s);
    CHECK(s.fmMicGainDb == std::optional<std::uint8_t>(12));
    CHECK(valueText(Item::FmMicGain, choiceOf(Item::FmMicGain, s)).view() == "+12 dB");
    CHECK(!s.dmrMicGainDb.has_value());
    setChoice(Item::FmMicGain, FromCodeplug + 20, s);
    CHECK(!s.fmMicGainDb.has_value());
}

TEST_CASE("Settings opens Audio with the two microphone gains")
{
    Navigator nav;
    nav.open(Group::Settings);
    moveTo(nav, 1);
    REQUIRE(nav.current().group == Group::Audio);
    CHECK(nav.press(Key::Menu) == Step::Opened);
    CHECK(groupTitle(Group::Audio).view() == "Settings > Audio");
    CHECK(nav.current().item == Item::DmrMicGain);
    CHECK(nav.press(Key::Down) == Step::Moved);
    CHECK(nav.current().item == Item::FmMicGain);
}
