// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "menu.h"

#include "dmm/services/gps.h"

#include <iterator>

namespace dmm::app::ui::menu {

namespace {

using services::Key;

constexpr Row TopRows[] = {Group::Channel, Group::Roaming, Group::Gps, Group::Display, Group::Settings, Group::Tools};
constexpr Row ChannelRows[] = {Item::Squelch, Item::SquelchMode, Item::Power};
constexpr Row RoamingRows[] = {Item::RoamStatus, Item::Roaming, Item::RoamZone};
constexpr Row GpsRows[] = {Item::GpsStatus, Item::Gps};
constexpr Row SettingsRows[] = {Group::Home, Group::Audio, Group::Keys, Group::Clock, Group::GpsSettings};
constexpr Row DisplayRows[] = {Item::Backlight, Item::BacklightTime};
constexpr Row ToolsRows[] = {Item::Log, Item::KeyTest, Item::Version};
constexpr Row HomeRows[] = {Item::HomePreset, Item::HomeTop, Item::HomeClock, Item::HomeBig, Item::HomeMiddle,
    Item::HomeBeside, Item::HomeBottom};
constexpr Row AudioRows[] = {Item::DmrMicGain, Item::FmMicGain};
constexpr Row KeysRows[] = {Item::KeyLock, Item::AutoLock, Item::BatterySave};
constexpr Row ClockRows[] = {Item::TimeZone};
constexpr Row GpsSettingsRows[] = {Item::GpsSystems};

constexpr GroupInfo Groups[] = {
    {"Menu", "", TopRows},
    {"Channel", "Squelch and power of the channel in use", ChannelRows},
    {"Roaming", "Moving between the roaming zone's repeaters", RoamingRows},
    {"GPS", "The position and the GPS receiver", GpsRows},
    {"Display", "Brightness and how long the light stays on", DisplayRows},
    {"Settings", "Home screen, audio, keys, clock and GPS", SettingsRows},
    {"Tools", "The log, the key test and the version", ToolsRows},
    {"Home screen", "What each row of the home screen shows", HomeRows},
    {"Audio", "The microphone gain on DMR and FM", AudioRows},
    {"Keys & battery", "The key lock and battery save", KeysRows},
    {"Clock", "The time zone", ClockRows},
    {"GPS", "The satellite systems the receiver uses", GpsSettingsRows},
};
static_assert(std::size(Groups) == static_cast<std::size_t>(Group::Count));

constexpr ItemInfo Items[] = {
    {"Squelch", "Open, or 1 to 5: higher needs a stronger signal", Kind::Value},
    {"Squelch type", "Carrier: any signal; CTCSS/DCS: its tone/code", Kind::Value},
    {"Power", "Transmit power of this channel", Kind::Value},
    {"Status", "Repeaters heard and their levels; search now", Kind::Screen},
    {"Roaming", "Off: the radio does nothing on its own", Kind::Value},
    {"Roam zone", "The repeaters to move between", Kind::Value},
    {"Status", "Position, satellites and time from the GPS", Kind::Screen},
    {"Receiver", "The GPS receiver on or off", Kind::Value},
    {"Brightness", "Display brightness", Kind::Value},
    {"Light time", "How long the light stays on after a key", Kind::Value},
    {"DMR mic gain", "Voice level sent on DMR, from the next call", Kind::Value},
    {"FM mic gain", "Voice level sent on FM, from the next call", Kind::Value},
    {"Lock covers", "What the key lock covers", Kind::Value},
    {"Auto lock", "Lock the keys after this long without use", Kind::Value},
    {"Battery save", "Sleep between checks for a signal while quiet", Kind::Value},
    {"Time zone", "Local time's offset from UTC", Kind::Value},
    {"Satellites", "Satellite systems the receiver uses", Kind::Value},
    {"Preset", "A whole layout at once; rows can then change", Kind::Value},
    {"Top line", "Small line at the top: talkgroup, zone, place", Kind::Value},
    {"Clock", "The time at the top right", Kind::Value},
    {"Large name", "The large name: channel, contact or zone", Kind::Value},
    {"Middle row", "Frequency, contact or signal under the name", Kind::Value},
    {"Next to power", "Voltage, volume or signal next to the power", Kind::Value},
    {"Bottom row", "Details, last caller or GPS position", Kind::Value},
    {"Log", "The radio's log; hold Exit for the next screen", Kind::Screen},
    {"Key test", "Shows each key pressed; hold Exit for the next", Kind::Screen},
    {"Version", "The firmware's version and time since start", Kind::Info},
};
static_assert(std::size(Items) == static_cast<std::size_t>(Item::Count));

// Choices with fixed texts.
struct Fixed
{
    std::int32_t value;
    std::string_view text;
    std::string_view note = {};
};
constexpr Fixed OffOn[] = {{0, "Off"}, {1, "On"}};
constexpr Fixed SquelchModes[] = {{static_cast<std::int32_t>(services::SquelchMode::Carrier), "Carrier"},
    {static_cast<std::int32_t>(services::SquelchMode::Tone), "CTCSS/DCS"}};
constexpr Fixed Powers[] = {{0, "Low"}, {1, "Mid"}, {2, "High"}, {3, "Turbo"}};
constexpr Fixed LockCovers[] = {{0, "All"}, {1, "Keys+PTT"}, {2, "Keypad"}, {3, "Side+PTT"}};
constexpr Fixed AutoLocks[] = {{0, "Off"}, {10, "10 s"}, {30, "30 s"}, {60, "60 s"}};
constexpr Fixed BatterySaves[] = {{0, "Off"}, {1, "On", "150 ms"}, {2, "Fast wake", "70 ms"}};
namespace g = services::gps_systems;
constexpr Fixed GpsSystems[] = {{g::Gps | g::Beidou, "GPS + BeiDou"}, {g::Gps, "GPS"}, {g::Beidou, "BeiDou"},
    {g::Gps | g::Glonass, "GPS + GLONASS"}, {g::Glonass, "GLONASS"}, {g::Beidou | g::Glonass, "BeiDou + GLONASS"},
    {g::All, "All three"}};
// The home screen's choices, numbered as services::HomeLayout's.
constexpr Fixed HomePresets[] = {{static_cast<std::int32_t>(services::HomePreset::Detailed), "Detailed"},
    {static_cast<std::int32_t>(services::HomePreset::Simple), "Simple"},
    {static_cast<std::int32_t>(services::HomePreset::Roaming), "Roaming"}};
constexpr Fixed HomeTops[] = {{0, "Talkgroup"}, {1, "Zone"}, {2, "Number"}, {3, "None"}};
constexpr Fixed HomeBigs[] = {{0, "Channel"}, {1, "Contact"}, {2, "Zone"}};
constexpr Fixed HomeMiddles[] = {{0, "Frequency"}, {1, "Talkgroup"}, {2, "Signal"}};
constexpr Fixed HomeBesides[] = {{0, "Voltage"}, {1, "Volume"}, {2, "Signal"}};
constexpr Fixed HomeBottoms[] = {{0, "Details"}, {1, "Last caller"}, {2, "Position"}, {3, "None"}};
static_assert(std::size(HomeTops) == static_cast<std::size_t>(services::HomeLayout::Top::Count));
static_assert(std::size(HomeBigs) == static_cast<std::size_t>(services::HomeLayout::Big::Count));
static_assert(std::size(HomeMiddles) == static_cast<std::size_t>(services::HomeLayout::Middle::Count));
static_assert(std::size(HomeBesides) == static_cast<std::size_t>(services::HomeLayout::Beside::Count));
static_assert(std::size(HomeBottoms) == static_cast<std::size_t>(services::HomeLayout::Bottom::Count));
// Light time choices, in seconds.
constexpr std::uint16_t BacklightTimes[] = {0, 5, 10, 15, 20, 25, 30, 60, 120, 180, 240, 300, 900, 1800, 2700, 3600};
// Time zones: UTC-12:00 to UTC+14:00 in half hours.
constexpr int ZoneFirstMinutes = -12 * 60;
constexpr int ZoneLastMinutes = 14 * 60;
constexpr int ZoneStepMinutes = 30;
constexpr int MaxLevel = 5;
namespace mic = services::microphone_gain;

std::span<const Fixed> fixedChoices(Item item)
{
    switch (item) {
    case Item::SquelchMode:
        return SquelchModes;
    case Item::Power:
        return Powers;
    case Item::Roaming:
    case Item::Gps:
    case Item::HomeClock:
        return OffOn;
    case Item::HomePreset:
        return HomePresets;
    case Item::HomeTop:
        return HomeTops;
    case Item::HomeBig:
        return HomeBigs;
    case Item::HomeMiddle:
        return HomeMiddles;
    case Item::HomeBeside:
        return HomeBesides;
    case Item::HomeBottom:
        return HomeBottoms;
    case Item::KeyLock:
        return LockCovers;
    case Item::AutoLock:
        return AutoLocks;
    case Item::BatterySave:
        return BatterySaves;
    case Item::GpsSystems:
        return GpsSystems;
    default:
        return {};
    }
}

// "Always", "5 s", "2 min", "1 h".
ValueText durationText(std::int32_t seconds)
{
    ValueText t;
    if (seconds == 0) {
        t << "Always";
    } else if (seconds >= 3600) {
        t << seconds / 3600 << " h";
    } else if (seconds >= 60) {
        t << seconds / 60 << " min";
    } else {
        t << seconds << " s";
    }
    return t;
}

// "UTC+3:00", "UTC-9:30", "UTC".
ValueText zoneText(std::int32_t minutes)
{
    ValueText t;
    t << "UTC";
    if (minutes != 0) {
        const std::int32_t m = minutes < 0 ? -minutes : minutes;
        t << (minutes < 0 ? '-' : '+') << m / 60 << ':' << lib::zeroPadded(static_cast<std::uint32_t>(m % 60), 2);
    }
    return t;
}

template <std::size_t N>
void copyText(char (&out)[N], std::string_view text)
{
    std::size_t n = 0;
    for (; n < text.size() && n + 1 < N; ++n) {
        out[n] = text[n];
    }
    out[n] = '\0';
}

} // namespace

const GroupInfo &info(Group group)
{
    return Groups[static_cast<std::size_t>(group)];
}

const ItemInfo &info(Item item)
{
    return Items[static_cast<std::size_t>(item)];
}

std::string_view label(Row row)
{
    return row.opensGroup() ? info(row.group).name : info(row.item).label;
}

std::string_view about(Row row)
{
    return row.opensGroup() ? info(row.group).about : info(row.item).about;
}

Place parentOf(Group group)
{
    for (std::size_t g = 0; g < std::size(Groups); ++g) {
        const std::span<const Row> rows = Groups[g].rows;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (rows[r].group == group) {
                return {static_cast<Group>(g), r};
            }
        }
    }
    return {Group::Count, 0};
}

Title groupTitle(Group group)
{
    Title t;
    const Place parent = parentOf(group);
    const std::string_view name = info(group).name;
    if (parent.group != Group::Count) {
        const std::string_view above = info(parent.group).name;
        if (above.size() + 3 + name.size() <= TitleChars) {
            t << above << " > ";
        }
    }
    t << name;
    return t;
}

Title valueTitle(Group group, Item item)
{
    Title t;
    const std::string_view name = info(group).name;
    const std::string_view text = info(item).label;
    if (name.size() + 3 + text.size() <= TitleChars) {
        t << name << " > ";
    }
    t << text;
    return t;
}

bool splitAbout(std::string_view text, std::string_view &first, std::string_view &second)
{
    if (text.size() <= AboutChars) {
        first = text;
        second = {};
        return true;
    }
    // The last space that leaves the first line short enough.
    std::size_t at = AboutChars;
    while (at > 0 && text[at] != ' ') {
        --at;
    }
    if (at == 0) {
        return false;
    }
    first = std::string_view(text.data(), at);
    second = std::string_view(text.data() + at + 1, text.size() - at - 1);
    return second.size() <= AboutChars;
}

Kept keptIn(Item item)
{
    switch (item) {
    case Item::SquelchMode:
    case Item::Power:
        return Kept::Channel;
    case Item::HomePreset:
    case Item::HomeTop:
    case Item::HomeClock:
    case Item::HomeBig:
    case Item::HomeMiddle:
    case Item::HomeBeside:
    case Item::HomeBottom:
    case Item::DmrMicGain:
    case Item::FmMicGain:
        return Kept::Preferences;
    case Item::Squelch:
    case Item::Roaming:
    case Item::RoamZone:
    case Item::Gps:
    case Item::Backlight:
    case Item::BacklightTime:
    case Item::KeyLock:
    case Item::AutoLock:
    case Item::BatterySave:
    case Item::TimeZone:
    case Item::GpsSystems:
        return Kept::Settings;
    case Item::RoamStatus:
    case Item::GpsStatus:
    case Item::Log:
    case Item::KeyTest:
    case Item::Version:
    case Item::Count:
        break;
    }
    return Kept::No;
}

std::int32_t choiceOf(Item item, const Settings &s)
{
    const services::KeyLockSettings &k = s.keyLock;
    switch (item) {
    case Item::Squelch:
        return s.squelch;
    case Item::SquelchMode:
        return static_cast<std::int32_t>(s.squelchMode);
    case Item::Power:
        return static_cast<std::int32_t>(s.power);
    case Item::Roaming:
        return s.roaming ? 1 : 0;
    case Item::RoamZone:
        return s.roamZone;
    case Item::Gps:
        return s.gps ? 1 : 0;
    case Item::Backlight:
        return s.backlight;
    case Item::BacklightTime:
        return s.backlightS;
    case Item::DmrMicGain:
        return s.dmrMicGainDb ? *s.dmrMicGainDb : FromCodeplug + s.plugDmrMicGainDb;
    case Item::FmMicGain:
        return s.fmMicGainDb ? *s.fmMicGainDb : FromCodeplug + s.plugFmMicGainDb;
    case Item::KeyLock:
        return k.knob ? 0 : k.keypad && k.side ? 1
            : k.keypad                         ? 2
                                               : 3;
    case Item::AutoLock:
        return static_cast<std::int32_t>(k.autoLockMs / 1000);
    case Item::BatterySave:
        return s.batterySave;
    case Item::TimeZone:
        return s.timeZoneMinutes;
    case Item::GpsSystems:
        return s.gpsSystems;
    case Item::HomePreset:
        return static_cast<std::int32_t>(services::presetOf(s.home));
    case Item::HomeTop:
        return static_cast<std::int32_t>(s.home.top);
    case Item::HomeClock:
        return s.home.clock ? 1 : 0;
    case Item::HomeBig:
        return static_cast<std::int32_t>(s.home.big);
    case Item::HomeMiddle:
        return static_cast<std::int32_t>(s.home.middle);
    case Item::HomeBeside:
        return static_cast<std::int32_t>(s.home.beside);
    case Item::HomeBottom:
        return static_cast<std::int32_t>(s.home.bottom);
    case Item::RoamStatus:
    case Item::GpsStatus:
    case Item::Log:
    case Item::KeyTest:
    case Item::Version:
    case Item::Count:
        break;
    }
    return 0;
}

void setChoice(Item item, std::int32_t value, Settings &s)
{
    const auto byte = static_cast<std::uint8_t>(value);
    switch (item) {
    case Item::Squelch:
        s.squelch = byte;
        break;
    case Item::SquelchMode:
        s.squelchMode = static_cast<services::SquelchMode>(byte);
        break;
    case Item::Power:
        s.power = static_cast<services::PowerLevel>(byte);
        break;
    case Item::Roaming:
        s.roaming = value != 0;
        break;
    case Item::RoamZone:
        s.roamZone = byte;
        break;
    case Item::Gps:
        s.gps = value != 0;
        break;
    case Item::Backlight:
        s.backlight = byte;
        break;
    case Item::BacklightTime:
        s.backlightS = static_cast<std::uint16_t>(value);
        break;
    case Item::DmrMicGain:
        s.dmrMicGainDb = value < FromCodeplug ? std::optional(byte) : std::nullopt;
        break;
    case Item::FmMicGain:
        s.fmMicGainDb = value < FromCodeplug ? std::optional(byte) : std::nullopt;
        break;
    case Item::KeyLock:
        s.keyLock.keypad = value != 3;
        s.keyLock.side = value != 2;
        s.keyLock.knob = value == 0;
        break;
    case Item::AutoLock:
        s.keyLock.autoLockMs = static_cast<std::uint32_t>(value) * 1000;
        break;
    case Item::BatterySave:
        s.batterySave = byte;
        break;
    case Item::TimeZone:
        s.timeZoneMinutes = static_cast<std::int16_t>(value);
        break;
    case Item::GpsSystems:
        s.gpsSystems = byte;
        break;
    case Item::HomePreset:
        s.home = services::presetLayout(static_cast<services::HomePreset>(byte));
        break;
    case Item::HomeTop:
        s.home.top = static_cast<services::HomeLayout::Top>(byte);
        break;
    case Item::HomeClock:
        s.home.clock = value != 0;
        break;
    case Item::HomeBig:
        s.home.big = static_cast<services::HomeLayout::Big>(byte);
        break;
    case Item::HomeMiddle:
        s.home.middle = static_cast<services::HomeLayout::Middle>(byte);
        break;
    case Item::HomeBeside:
        s.home.beside = static_cast<services::HomeLayout::Beside>(byte);
        break;
    case Item::HomeBottom:
        s.home.bottom = static_cast<services::HomeLayout::Bottom>(byte);
        break;
    case Item::RoamStatus:
    case Item::GpsStatus:
    case Item::Log:
    case Item::KeyTest:
    case Item::Version:
    case Item::Count:
        break;
    }
}

ValueText valueText(Item item, std::int32_t value)
{
    ValueText t;
    switch (item) {
    case Item::Squelch:
        if (value == 0) {
            t << "Open";
        } else {
            t << value;
        }
        return t;
    case Item::Backlight:
        t << value;
        return t;
    case Item::BacklightTime:
        return durationText(value);
    case Item::DmrMicGain:
    case Item::FmMicGain:
        if (value >= FromCodeplug) {
            t << "Plug ";
            value -= FromCodeplug;
        }
        t << '+' << value << " dB";
        return t;
    case Item::AutoLock:
        if (value == 0) {
            t << "Off";
        } else {
            t << value << " s";
        }
        return t;
    case Item::TimeZone:
        return zoneText(value);
    case Item::GpsSystems:
        t << services::gps_systems::name(static_cast<std::uint8_t>(value));
        return t;
    case Item::HomePreset:
        if (value == static_cast<std::int32_t>(services::HomePreset::Own)) {
            t << "Own";
            return t;
        }
        break;
    default:
        break;
    }
    for (const Fixed &f : fixedChoices(item)) {
        if (f.value == value) {
            t << f.text;
        }
    }
    return t;
}

void Choices::add(std::int32_t value, std::string_view text, std::string_view note)
{
    if (m_count >= MaxChoices) {
        return;
    }
    m_value[m_count] = value;
    copyText(m_text[m_count], text);
    copyText(m_note[m_count], note);
    ++m_count;
}

bool Choices::hasNotes() const
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_note[i][0] != '\0') {
            return true;
        }
    }
    return false;
}

std::size_t Choices::find(std::int32_t value) const
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_value[i] == value) {
            return i;
        }
    }
    return m_count;
}

bool listChoices(Item item, Choices &out, const Settings &s)
{
    out.clear();
    switch (item) {
    case Item::RoamZone:
        return false;
    case Item::Squelch:
        for (std::int32_t level = 0; level <= MaxLevel; ++level) {
            out.add(level, valueText(item, level).view());
        }
        return true;
    case Item::Backlight:
        for (std::int32_t level = 1; level <= MaxLevel; ++level) {
            out.add(level, valueText(item, level).view());
        }
        return true;
    case Item::BacklightTime:
        for (const std::uint16_t seconds : BacklightTimes) {
            out.add(seconds, durationText(seconds).view());
        }
        return true;
    case Item::TimeZone:
        for (int m = ZoneFirstMinutes; m <= ZoneLastMinutes; m += ZoneStepMinutes) {
            out.add(m, zoneText(m).view());
        }
        return true;
    case Item::DmrMicGain:
    case Item::FmMicGain: {
        const bool dmr = item == Item::DmrMicGain;
        const std::int32_t plug = FromCodeplug + (dmr ? s.plugDmrMicGainDb : s.plugFmMicGainDb);
        ValueText text;
        text << "Codeplug " << valueText(item, plug - FromCodeplug).view();
        out.add(plug, text.view());
        for (std::int32_t db = mic::MinDb; db <= (dmr ? mic::MaxDigitalDb : mic::MaxFmDb); ++db) {
            out.add(db, valueText(item, db).view());
        }
        return true;
    }
    default:
        break;
    }
    for (const Fixed &f : fixedChoices(item)) {
        out.add(f.value, f.text, f.note);
    }
    return true;
}

Style styleOf(const Choices &choices)
{
    if (choices.hasNotes()) {
        return Style::List;
    }
    return choices.size() <= 4 ? Style::Buttons : Style::Spinner;
}

void Navigator::open(Group group, bool quick)
{
    m_open = true;
    m_quick = quick;
    m_choosing = false;
    m_group = group;
}

void Navigator::choose(std::int32_t inUse)
{
    const std::size_t at = m_choices.find(inUse);
    m_inUse = at;
    m_cursor = at < m_choices.size() ? at : 0;
    m_choosing = m_choices.size() > 0;
}

Navigator::Step Navigator::press(Key key)
{
    if (!m_open) {
        return Step::None;
    }
    switch (key) {
    case Key::Up:
    case Key::Down: {
        // A large value goes up with Up; lists and buttons move as drawn.
        const int up = m_choosing && style() == Style::Spinner ? 1 : -1;
        return move(key == Key::Up ? up : -up);
    }
    case Key::Menu:
        return select();
    case Key::Exit:
        return back();
    default:
        return Step::None;
    }
}

Navigator::Step Navigator::turn(int steps)
{
    return m_open ? move(steps) : Step::None;
}

Navigator::Step Navigator::move(int steps)
{
    const int n = static_cast<int>(m_choosing ? m_choices.size() : groupInfo().rows.size());
    if (n == 0 || steps == 0) {
        return Step::None;
    }
    const int from = static_cast<int>(m_choosing ? m_cursor : row());
    const int to = ((from + steps) % n + n) % n;
    if (m_choosing) {
        m_cursor = static_cast<std::size_t>(to);
    } else {
        m_rows[static_cast<std::size_t>(m_group)] = static_cast<std::uint8_t>(to);
    }
    return Step::Moved;
}

Navigator::Step Navigator::select()
{
    if (m_choosing) {
        m_choosing = false;
        if (m_quick) {
            m_open = false;
        }
        return Step::Set;
    }
    const Row r = current();
    if (r.opensGroup()) {
        m_group = r.group;
        return Step::Opened;
    }
    switch (info(r.item).kind) {
    case Kind::Value:
        return Step::Choose;
    case Kind::Screen:
        return Step::Show;
    case Kind::Info:
        break;
    }
    return Step::None;
}

Navigator::Step Navigator::back()
{
    if (m_choosing) {
        m_choosing = false;
        return Step::Opened;
    }
    const Place parent = parentOf(m_group);
    if (m_quick || parent.group == Group::Count) {
        m_open = false;
        return Step::Left;
    }
    m_group = parent.group;
    m_rows[static_cast<std::size_t>(m_group)] = static_cast<std::uint8_t>(parent.row);
    return Step::Opened;
}

std::string_view Navigator::hint() const
{
    if (m_choosing) {
        return "Menu: set   Exit: back";
    }
    const Row r = current();
    if (r.opensGroup() || info(r.item).kind == Kind::Screen) {
        return "Menu: open   Exit: back";
    }
    if (info(r.item).kind == Kind::Value) {
        return "Menu: change   Exit: back";
    }
    return "Exit: back";
}

} // namespace dmm::app::ui::menu
