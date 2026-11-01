// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "ui.h"

#include "audio.h"
#include "gps.h"
#include "menu.h"
#include "menu_screen.h"
#include "radio.h"
#include "standby.h"
#include "system.h"

#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/channel_selector.h"
#include "dmm/services/clock.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/home_screen.h"
#include "dmm/services/input.h"
#include "dmm/services/log.h"
#include "dmm/services/microphone_gain.h"
#include "dmm/services/power.h"
#include "dmm/version.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string_view>

namespace dmm::app::ui {

namespace {

using namespace services;
using services::log::Level;
namespace log = services::log;

os::Task<1088> uiTask;
os::Queue<Message, 16> inbox;
Display *display = nullptr;
CodeplugSource *codeplug = nullptr;
const board::Input *input = nullptr;
board::SettingsStore *settingsStore = nullptr;

// A channel read from the codeplug, with the values still waiting in the
// settings store laid over it.
void takeKeptChannel(std::uint16_t index, Channel &c)
{
    if (settingsStore == nullptr) {
        return;
    }
    if (const auto kept = settingsStore->keptChannel(index)) {
        c.power = kept->power;
        c.squelchMode = kept->squelchMode;
    }
}

// The label on @p key as the user sees it.
std::string_view keyText(Key key)
{
    return input != nullptr ? input->keyLabel(key) : keyName(key);
}

// The header band's colour: black on the development views, the status
// bar's grey on the radio's own screens.
Color headerGround = colors::Black;

constexpr int DefaultBacklight = 4;
// How long the start screen shows the name and version.
constexpr os::Ticks StartScreenMs = 1200;
constexpr os::Ticks RedrawStepMs = 100;

// The log view: a header in the small font, then rows of wrapped log text.
const Font &HeaderFont = fonts::Spleen5x8;
const Font &LogFont = fonts::Spleen6x12;
constexpr int MaxColumns = 32;
constexpr int MaxRows = 12;

struct Row
{
    char text[MaxColumns];
    std::uint8_t length;
    Level level;
};

class LogView
{
public:
    void configure(int columns, int rows)
    {
        m_columns = columns < MaxColumns ? columns : MaxColumns;
        m_rows = rows < MaxRows ? rows : MaxRows;
    }

    // Called from any task that logs (log tap).
    void add(Level level, std::string_view line)
    {
        // Drop the time stamp: the header shows the uptime.
        const std::size_t close = line.find("] ");
        if (line.starts_with('[') && close != std::string_view::npos) {
            line.remove_prefix(close + 2);
        }
        m_lock.lock();
        do {
            const std::size_t n = line.size() < static_cast<std::size_t>(m_columns) ? line.size() : static_cast<std::size_t>(m_columns);
            Row &row = m_ring[m_next];
            for (std::size_t i = 0; i < n; ++i) {
                row.text[i] = line[i];
            }
            row.length = static_cast<std::uint8_t>(n);
            row.level = level;
            m_next = (m_next + 1) % MaxRows;
            if (m_count < MaxRows) {
                ++m_count;
            }
            line.remove_prefix(n);
        } while (!line.empty());
        m_dirty = true;
        m_lock.unlock();
    }

    // Draws the rows if anything changed; the newest at the bottom.
    void draw(Display &d, int top, bool force)
    {
        Row rows[MaxRows];
        int count = 0;
        m_lock.lock();
        const bool dirty = m_dirty || force;
        m_dirty = false;
        if (dirty) {
            count = m_count < m_rows ? m_count : m_rows;
            for (int i = 0; i < count; ++i) {
                rows[i] = m_ring[(m_next + MaxRows - count + i) % MaxRows];
            }
        }
        m_lock.unlock();
        if (!dirty) {
            return;
        }
        const int first = m_rows - count;
        for (int r = 0; r < m_rows; ++r) {
            const int y = top + r * LogFont.height;
            int x = 0;
            if (r >= first) {
                const Row &row = rows[r - first];
                x = drawText(d, LogFont, 0, y, std::string_view(row.text, row.length), colorFor(row.level), colors::Black);
            }
            d.fillRect(x, y, d.width() - x, LogFont.height, colors::Black);
        }
    }

private:
    static Color colorFor(Level level)
    {
        switch (level) {
        case Level::Debug:
            return colors::Grey;
        case Level::Warning:
            return colors::Yellow;
        case Level::Error:
            return rgb(255, 80, 80);
        case Level::Info:
            break;
        }
        return colors::White;
    }

    os::Mutex m_lock;
    Row m_ring[MaxRows]{};
    int m_next = 0;
    int m_count = 0;
    int m_columns = 26;
    int m_rows = 10;
    bool m_dirty = false;
};

LogView logView;

void onLogLine(Level level, std::string_view line)
{
    logView.add(level, line);
}

// The last battery state from the system task; millivolts 0 = unknown.
struct BatteryState
{
    int millivolts = 0;
    int level = 0;
    bool low = false;
};

BatteryState batteryState;

// Right-aligned in the header: a battery icon.
// The radio's own screens put it in their taller status bar.
int headerHeight = 8;
int headerTop = 0;

// A battery at the header's right end, in its own field from 48 pixels
// before the edge: filled from the left by its level, so the outline shows
// as it drains, red when low. Every pixel is painted once, so an update
// does not flicker.
void drawBattery(Display &d)
{
    const BatteryState &b = batteryState;
    const int w = d.width();
    const int fieldX = w - 48;
    constexpr int BodyW = 21;
    constexpr int InnerW = BodyW - 2;
    const int x = w - 5 - BodyW;
    const int y = headerTop == 0 ? 0 : 1;
    const int bodyH = std::min(9, headerHeight - y);
    const Color ink = b.low ? colors::Red : colors::White;
    int filled = 0;
    if (b.millivolts > 0) {
        // A low battery keeps a sliver.
        filled = std::max(b.level * InnerW / 3, b.low ? 3 : 0);
    }
    fillArea(d, fieldX, 0, x - fieldX, headerHeight, headerGround);
    fillArea(d, x, 0, w - x, y, headerGround);
    fillArea(d, x, y + bodyH, w - x, headerHeight - y - bodyH, headerGround);
    d.fillRect(x, y, BodyW, 1, ink);
    d.fillRect(x, y + bodyH - 1, BodyW, 1, ink);
    d.fillRect(x, y + 1, 1, bodyH - 2, ink);
    d.fillRect(x + BodyW - 1, y + 1, 1, bodyH - 2, ink);
    fillArea(d, x + 1, y + 1, filled, bodyH - 2, ink);
    fillArea(d, x + 1 + filled, y + 1, InnerW - filled, bodyH - 2, headerGround);
    // The terminal.
    d.fillRect(x + BodyW, y + 2, 2, bodyH - 4, ink);
    fillArea(d, x + BodyW, y, 2, 2, headerGround);
    fillArea(d, x + BodyW, y + bodyH - 2, 2, 2, headerGround);
    fillArea(d, x + BodyW + 2, y, w - x - BodyW - 2, bodyH, headerGround);
}

services::KeyLock keyLock;
bool transmitting = false;

// The header's lock mark, left of the battery: a yellow LOCK box while the
// keys are locked, blank otherwise. Header text must end before LockMarkX.
constexpr int LockMarkW = 4 * 5 + 3;
int lockMarkX(const Display &d)
{
    return d.width() - 48 - LockMarkW;
}

void drawLockMark(Display &d)
{
    const int x = lockMarkX(d);
    if (keyLock.locked() || keyLock.settings().forced) {
        d.fillRect(x, 0, LockMarkW, HeaderFont.height, colors::Yellow);
        drawText(d, HeaderFont, x + 1, 0, "LOCK", colors::Black, colors::Yellow);
    } else {
        d.fillRect(x, 0, LockMarkW, HeaderFont.height, colors::Black);
    }
}

void drawHeader(Display &d)
{
    lib::StaticString<40> text;
    text << Version << " up " << (os::nowMs() / 1000) << "s";
    const int x = drawText(d, HeaderFont, 0, 0, text.view(), colors::Grey, colors::Black);
    d.fillRect(x, 0, lockMarkX(d) - x, HeaderFont.height, colors::Black);
    drawLockMark(d);
    drawBattery(d);
}

// A short notice in a band across the middle of the screen, drawn over
// what is there without blanking it first.
void drawNotice(Display &d, std::string_view text)
{
    const Font &font = fonts::Spleen8x16;
    const int h = font.height + 10;
    const int y = (d.height() - h) / 2;
    const int w = d.width();
    const int textW = static_cast<int>(text.size()) * font.width;
    const int tx = (w - textW) / 2;
    const int ty = y + 5;
    drawText(d, font, tx, ty, text, palette::Text, palette::Ground);
    d.fillRect(0, y, w, 1, palette::Rule);
    fillArea(d, 0, y + 1, w, ty - y - 1, palette::Ground);
    fillArea(d, 0, ty, tx, font.height, palette::Ground);
    fillArea(d, tx + textW, ty, w - tx - textW, font.height, palette::Ground);
    fillArea(d, 0, ty + font.height, w, y + h - 1 - ty - font.height, palette::Ground);
    d.fillRect(0, y + h - 1, w, 1, palette::Rule);
}

// The volume while the knob turns: "Volume" and a 15-segment bar. With
// @p frame the whole band is drawn over what is there; without, only the
// number and the segments change, in place.
void drawVolumeNotice(Display &d, int step, bool frame)
{
    const Font &font = fonts::Spleen6x12;
    const int h = font.height + 16;
    const int y = (d.height() - h) / 2;
    const int w = d.width();
    const int labelY = y + 3;
    const int segY = y + font.height + 6;
    constexpr int SegH = 5;
    constexpr int LabelChars = 9; // "Volume 15"
    lib::StaticString<12> label;
    label << "Volume " << step;
    while (label.view().size() < LabelChars) {
        label << ' ';
    }
    drawText(d, font, 6, labelY, label.view(), palette::Text, palette::Ground);
    constexpr int Segments = 15;
    const int segW = (w - 12) / Segments;
    for (int i = 0; i < Segments; ++i) {
        d.fillRect(6 + i * segW, segY, segW - 2, SegH, i < step ? palette::Text : palette::Rule);
    }
    if (!frame) {
        return;
    }
    const int labelEnd = 6 + LabelChars * font.width;
    d.fillRect(0, y, w, 1, palette::Rule);
    fillArea(d, 0, y + 1, w, labelY - y - 1, palette::Ground);
    fillArea(d, 0, labelY, 6, font.height, palette::Ground);
    fillArea(d, labelEnd, labelY, w - labelEnd, font.height, palette::Ground);
    fillArea(d, 0, labelY + font.height, w, segY - labelY - font.height, palette::Ground);
    fillArea(d, 0, segY, 6, SegH, palette::Ground);
    for (int i = 0; i < Segments; ++i) {
        fillArea(d, 6 + i * segW + segW - 2, segY, 2, SegH, palette::Ground);
    }
    fillArea(d, 6 + Segments * segW, segY, w - 6 - Segments * segW, SegH, palette::Ground);
    fillArea(d, 0, segY + SegH, w, y + h - 1 - segY - SegH, palette::Ground);
    d.fillRect(0, y + h - 1, w, 1, palette::Rule);
}

void logLockSettings()
{
    const services::KeyLockSettings &s = keyLock.settings();
    log::info("ui") << "key lock " << (keyLock.locked() ? "on" : "off") << "; covers" << (s.knob ? " knob" : "")
                    << (s.keypad ? " keypad" : "") << (s.side ? " side-keys+PTT" : "") << (s.forced ? ", forced" : "")
                    << ", auto " << s.autoLockMs / 1000 << " s";
}

// The start screen: the name and version on black.
// Under the version while the start screen waits for the radio.
void drawStarting(Display &d)
{
    const Font &small = fonts::Spleen6x12;
    constexpr std::string_view Text = "Please wait...";
    const int w = static_cast<int>(Text.size()) * small.width;
    drawText(d, small, (d.width() - w) / 2, d.height() - small.height - 8, Text, colors::Grey, colors::Black);
}

void drawStartScreen(Display &d)
{
    d.fillRect(0, 0, d.width(), d.height(), colors::Black);
    const Font &big = fonts::Spleen12x24;
    const Font &small = fonts::Spleen6x12;
    constexpr std::string_view Name = "DeltaMM";
    const int nameW = static_cast<int>(Name.size()) * big.width;
    const int y = (d.height() - big.height - small.height - 6) / 2;
    drawText(d, big, (d.width() - nameW) / 2, y, Name, colors::White, colors::Black);
    const std::string_view version(Version);
    const int versionW = static_cast<int>(version.size()) * small.width;
    drawText(d, small, (d.width() - versionW) / 2, y + big.height + 6, version, colors::Grey, colors::Black);
}

// Colour bars, a white frame, corner marks and sample text in each font,
// so position, orientation, colour order and glyphs can be checked by eye.
void drawTestPattern(Display &d)
{
    const Color bars[] = {colors::White, colors::Yellow, colors::Cyan, colors::Green, colors::Magenta, colors::Red,
        colors::Blue, colors::Black};
    const int w = d.width();
    const int h = d.height();
    for (int i = 0; i < 8; ++i) {
        d.fillRect(i * w / 8, 0, (i + 1) * w / 8 - i * w / 8, h, bars[i]);
    }
    d.fillRect(0, 0, w, 1, colors::White);
    d.fillRect(0, h - 1, w, 1, colors::White);
    d.fillRect(0, 0, 1, h, colors::White);
    d.fillRect(w - 1, 0, 1, h, colors::White);
    const Font &small = fonts::Spleen5x8;
    drawText(d, small, 2, 2, "TL", colors::Black, colors::White);
    drawText(d, small, w - 12, 2, "TR", colors::White, colors::Black);
    drawText(d, small, 2, h - 10, "BL", colors::Black, colors::White);
    drawText(d, small, w - 12, h - 10, "BR", colors::White, colors::Black);
    drawText(d, fonts::Spleen12x24, 2 + 12 * 2, 24, "DeltaMM", colors::Black, colors::White);
    drawText(d, fonts::Spleen8x16, 8, 56, "8x16 äöå ÄÖÅ", colors::White, colors::Black);
    drawText(d, fonts::Spleen6x12, 8, 76, "6x12 Hyvää päivää!", colors::White, colors::Black);
    drawText(d, fonts::Spleen5x8, 8, 92, "5x8 The quick brown fox", colors::White, colors::Black);
}

enum class View : std::uint8_t
{
    Start, // name and version while the radio starts
    TestPattern,
    Standby,    // the radio's main screen
    ZonePicker, // choosing a zone
    RoamStatus, // what roaming knows, refreshed each second
    Menu,       // the menu's lists and value screens
    Gps,        // the GPS receiver's state and fix, from the menu
    Log,
    KeyTest,
    Message,
};

// The radio's working channel, from the codeplug; nullptr without one.
ChannelSelector *channels = nullptr;
StandbyScreen standby;
ListPicker zonePicker;
menu::Navigator menuNav;

// The screens that show a list; the roaming status screen borrows the
// zone list's.
bool isList(View v)
{
    return v == View::ZonePicker || v == View::RoamStatus || v == View::Menu;
}

int backlightLevel = DefaultBacklight;
std::uint16_t backlightS = 0; // 0 = always on
bool backlightLit = true;
os::Ticks lastLightUse = 0;
std::uint8_t batterySave = 0;
// The GPS receiver as last reported, and the GPS setting: the codeplug's
// choice at start, then the menu's.
GpsState gpsState;
bool gpsAtStart = false;
std::uint8_t gpsSystems = services::gps_systems::Gps | services::gps_systems::Beidou;
// Local time minus UTC, from the codeplug, in half-hour steps.
int timeZoneMinutes = 0;

// The local time at UTC @p utc as "HH:MM".
lib::StaticString<6> localClockText(std::uint32_t utc)
{
    lib::StaticString<6> s;
    const services::DateTime t = services::dateTime(static_cast<std::uint32_t>(static_cast<std::int64_t>(utc) + timeZoneMinutes * 60));
    s << lib::zeroPadded(t.hour, 2) << ':' << lib::zeroPadded(t.minute, 2);
    return s;
}
// What each row of the home screen shows.
services::HomeLayout homeLayout;
// The volume step 0..15, -1 until the knob has been read.
int volumeLevel = -1;
bool receiving = false;
// The DMR call on the call screen: while it is heard, then for a moment
// after it ends, unless a key or a channel change sends the screen back.
struct HeardCall
{
    bool shown = false;
    bool active = false;
    bool group = false;
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    // The caller's callsign (or contact name) and the user's name.
    char caller[CodeplugSource::ContactNameBytes + 1] = {};
    char callerName[CodeplugSource::ContactNameBytes + 1] = {};
    char target[CodeplugSource::ContactNameBytes + 1] = {};
    os::Ticks started = 0;
    os::Ticks ended = 0;
};
HeardCall heard;
char callChannel[Channel::NameBytes + 1] = {};
// The last call heard, for the home screen: the caller's name (empty when
// not known), ID, and when the call ended (0 for none).
char lastCaller[CodeplugSource::ContactNameBytes + 1] = {};
std::uint32_t lastCallerId = 0;
os::Ticks lastCallAt = 0;
// Roaming: the roaming zone, how many repeaters it has, and the repeater
// the radio task last reported.
std::uint8_t roamingZone = services::RoamingSettings::NoZone;
std::size_t roamingSites = 0;
// Auto roaming: the codeplug's choice at start, then the menu's.
bool autoRoaming = false;
// Microphone gain on DMR and FM: the codeplug's levels in dB, and the dB
// set on the radio, which wins.
std::uint8_t plugDmrMicGainDb = services::microphone_gain::StandardDb;
std::uint8_t plugFmMicGainDb = services::microphone_gain::StandardDb;
std::optional<std::uint8_t> dmrMicGainDb;
std::optional<std::uint8_t> fmMicGainDb;

void postMicrophoneGain()
{
    radio::post(radio::MicrophoneGain{static_cast<std::int8_t>(dmrMicGainDb.value_or(plugDmrMicGainDb)),
        static_cast<std::int8_t>(fmMicGainDb.value_or(plugFmMicGainDb))});
}

// "+20 dB", or "codeplug" for none.
lib::StaticString<10> keptGainText(std::optional<std::uint8_t> db)
{
    lib::StaticString<10> t;
    if (db) {
        t << '+' << *db << " dB";
    } else {
        t << "codeplug";
    }
    return t;
}

// "microphone gain: DMR +8 dB (codeplug), FM +20 dB".
void logMicrophoneGain()
{
    log::info("ui") << "microphone gain: DMR +" << dmrMicGainDb.value_or(plugDmrMicGainDb) << " dB"
                    << (dmrMicGainDb ? "" : " (codeplug)") << ", FM +" << fmMicGainDb.value_or(plugFmMicGainDb) << " dB"
                    << (fmMicGainDb ? "" : " (codeplug)");
}
struct RoamState
{
    bool searching = false;
    bool lost = false;
    int site = -1;
    std::uint32_t receiveHz = 0;
    std::uint8_t colourCode = 0;
    char name[CodeplugSource::SiteNameBytes + 1] = {};
};
RoamState roam;
// The radio task's roaming state, copied for the status screen and its
// menu row; kept here, not on the stack.
radio::RoamingView roamView;
// Hands the roaming zone's repeaters to the radio task, which then looks
// for one afresh.
void loadRoamingSites()
{
    radio::setRoamingSites([](std::span<services::RoamingSite> out) {
        roamingSites = roamingZone != services::RoamingSettings::NoZone && codeplug != nullptr
            ? codeplug->roamingSites(roamingZone, out)
            : 0;
        return roamingSites;
    });
}
// The repeater in use and how it is doing, as "2 Good", or what roaming
// is doing instead.
lib::StaticString<9> roamStateText(const radio::RoamingView &v)
{
    constexpr const char *States[] = {"Good", "Weak", "Quiet", "Lost"};
    lib::StaticString<9> t;
    if (!v.roaming) {
        t << "Off";
    } else if (v.searching) {
        t << "Search";
    } else if (v.site < 0) {
        t << "None";
    } else {
        t << v.site + 1 << ' ' << States[static_cast<int>(v.health)];
    }
    return t;
}
// Fills @p list with what roaming knows: the repeater in use, then each
// repeater heard with its level and the age of that level, and a row that
// searches now. The cursor stays on the same row.
constexpr std::uint8_t SearchNowRow = 0xFF;
void fillRoamStatus(ListPicker &list)
{
    radio::RoamingView &v = roamView;
    radio::roamingView(v);
    const std::uint8_t at = list.empty() ? 0 : list.chosen();
    list.open("Roaming");
    list.add(0, "In use", roamStateText(v).view());
    const std::uint32_t now = os::nowMs();
    for (std::uint8_t k = 0; k < v.count; ++k) {
        const services::roaming::HeardList::Entry &e = v.entries[k];
        // "2*438.2125": position, a star for the one in use, output
        // frequency.
        lib::StaticString<24> text;
        const std::uint32_t frac = v.receiveHz[k] / 100 % 10'000;
        text << e.position + 1 << (e.position == v.site ? '*' : ' ') << v.receiveHz[k] / 1'000'000 << '.'
             << static_cast<char>('0' + frac / 1000) << static_cast<char>('0' + frac / 100 % 10)
             << static_cast<char>('0' + frac / 10 % 10);
        if (frac % 10 != 0) {
            text << static_cast<char>('0' + frac % 10);
        }
        // "-35 12s": level and its age; "--- 12s" for one in use that has
        // not answered and has no level.
        const std::uint32_t age = (now - e.measuredMs) / 1000;
        lib::StaticString<12> value;
        if (e.samples > 0) {
            value << e.levelDbm << ' ';
        } else {
            value << "--- ";
        }
        if (age < 100) {
            value << age << 's';
        } else if (age < 6000) {
            value << age / 60 << 'm';
        } else {
            value << "old";
        }
        list.add(static_cast<std::uint8_t>(k + 1), text.view(), value.view());
    }
    list.add(SearchNowRow, "Search now");
    list.select(at);
}
// The side keys' functions, and whether a long press has already acted.
services::KeyFunction sideShort[CodeplugSettings::SideKeys] = {};
services::KeyFunction sideLong[CodeplugSettings::SideKeys] = {};
bool sideLongDone[CodeplugSettings::SideKeys] = {};
constexpr os::Ticks CallHoldMs = 5000;
// How long the home screen shows a signal's level after it was last read.
constexpr os::Ticks SignalHoldMs = 2000;
constexpr std::uint32_t AllCallId = 0xFFFFFF;

template <std::size_t N>
void copyName(char (&out)[N], const char *text)
{
    std::size_t n = 0;
    for (; n + 1 < N && text[n] != '\0'; ++n) {
        out[n] = text[n];
    }
    out[n] = '\0';
}

// The channel's contact as the home screen names it ("TG 9", and the
// contact's name), looked up again only when the contact changes.
char talkgroupText[16] = {};
char talkgroupName[CodeplugSource::ContactNameBytes + 1] = {};
services::dmr::Target talkgroupFor{services::dmr::CallType::All, 0xFFFFFFFF};

// A DMR channel with a contact to call.
bool hasTalkgroup(const Channel &c)
{
    return c.digital() && (c.dmr.contact.id != 0 || c.dmr.contact.type == services::dmr::CallType::All);
}

void nameTalkgroup(const services::dmr::Target &t)
{
    if (t == talkgroupFor) {
        return;
    }
    talkgroupFor = t;
    lib::StaticString<16> text;
    if (t.type == services::dmr::CallType::All) {
        text << "ALL CALL";
    } else {
        text << (t.type == services::dmr::CallType::Group ? "TG " : "PC ") << t.id;
    }
    const std::size_t n = std::min(text.view().size(), sizeof(talkgroupText) - 1);
    std::copy_n(text.view().data(), n, talkgroupText);
    talkgroupText[n] = '\0';
    talkgroupName[0] = '\0';
    std::array<char, CodeplugSource::ContactNameBytes + 1> name{};
    if (t.type != services::dmr::CallType::All && codeplug != nullptr &&
        codeplug->contactName(t.id, t.type == services::dmr::CallType::Group, name)) {
        copyName(talkgroupName, name.data());
    }
}

// Names for a call from the codeplug: the caller's own contact entry
// first, else the user directory's callsign and name.
void nameCall(HeardCall &h, CodeplugSource *plug)
{
    h.caller[0] = h.callerName[0] = h.target[0] = '\0';
    if (plug == nullptr) {
        return;
    }
    std::array<char, CodeplugSource::ContactNameBytes + 1> name{};
    if (plug->contactName(h.destination, h.group, name)) {
        copyName(h.target, name.data());
    }
    CodeplugSource::DmrUser user;
    const bool known = plug->dmrUser(h.source, user);
    if (plug->contactName(h.source, false, name)) {
        copyName(h.caller, name.data());
    } else if (known && user.callsign[0] != '\0') {
        copyName(h.caller, user.callsign.data());
    } else if (known) {
        copyName(h.caller, user.name.data());
        return;
    }
    if (known && h.caller[0] != '\0' && std::string_view(user.name.data()) != h.caller) {
        copyName(h.callerName, user.name.data());
    }
}
// The standby screen's quick-key hint.
lib::StaticString<40> quickKeys;
// Frequency digits typed on the standby screen.
constexpr std::size_t EntryDigits = 6;
char entry[EntryDigits + 1] = {};
std::size_t entryLength = 0;
constexpr std::uint32_t VfoStepHz = 12'500;

// The key test: every key as a box in panel order, lit while pressed, and
// the knob values below.
class KeyTestView
{
public:
    void draw(Display &d)
    {
        d.fillRect(0, 0, d.width(), d.height(), colors::Black);
        drawText(d, HeaderFont, 0, 0, "Keys (Exit=log)", colors::Grey, colors::Black);
        drawLockMark(d);
        drawBattery(d);
        for (unsigned i = 0; i < static_cast<unsigned>(Key::Count); ++i) {
            drawKey(d, static_cast<Key>(i));
        }
        drawValues(d);
    }

    void setKeys(Display &d, KeyMask keys, bool visible)
    {
        const KeyMask changed = keys ^ m_keys;
        m_keys = keys;
        if (!visible) {
            return;
        }
        for (unsigned i = 0; i < static_cast<unsigned>(Key::Count); ++i) {
            if ((changed & (1u << i)) != 0) {
                drawKey(d, static_cast<Key>(i));
            }
        }
    }

    void setClicks(std::int32_t v) { m_clicks = v; }
    void setRaw(std::int32_t v) { m_raw = v; }
    void setVolume(std::int32_t v) { m_volume = v; }

    void drawValues(Display &d)
    {
        lib::StaticString<40> line;
        line << "Knob " << m_clicks << " (raw " << m_raw << ")";
        valueLine(d, ValuesTop, line.view());
        line.clear();
        if (m_volume < 0) {
            line << "Volume: none";
        } else {
            line << "Volume " << m_volume / 10 << "." << m_volume % 10 << " %";
        }
        valueLine(d, ValuesTop + LogFont.height, line.view());
    }

private:
    static constexpr int Columns = 6;
    static constexpr int BoxW = 26;
    static constexpr int BoxH = 13;
    static constexpr int GridTop = 11;
    static constexpr int ValuesTop = GridTop + 4 * (BoxH + 2) + 4;

    // Panel order: side keys and PTT, the four control keys, then digits.
    static constexpr Key Layout[] = {Key::Ptt, Key::Side1, Key::Side2, Key::Side3, Key::Front1, Key::Front2, Key::Menu, Key::Up,
        Key::Down, Key::Exit, Key::Star, Key::Hash, Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
        Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9, Key::Digit0};

    void drawKey(Display &d, Key key)
    {
        int slot = 0;
        while (Layout[slot] != key) {
            ++slot;
        }
        const int x = (slot % Columns) * (BoxW + 1);
        const int y = GridTop + (slot / Columns) * (BoxH + 2);
        const bool down = (m_keys & keyBit(key)) != 0;
        const Color fill = down ? colors::Green : rgb(48, 48, 56);
        const Color ink = down ? colors::Black : colors::White;
        d.fillRect(x, y, BoxW, BoxH, fill);
        const std::string_view name = keyText(key);
        const int textW = static_cast<int>(name.size()) * HeaderFont.width;
        drawText(d, HeaderFont, x + (BoxW - textW) / 2, y + 3, name, ink, fill);
    }

    static void valueLine(Display &d, int y, std::string_view text)
    {
        const int x = drawText(d, LogFont, 0, y, text, colors::White, colors::Black);
        d.fillRect(x, y, d.width() - x, LogFont.height, colors::Black);
    }

    KeyMask m_keys = 0;
    std::int32_t m_clicks = 0;
    std::int32_t m_raw = 0;
    std::int32_t m_volume = -1;
};

KeyTestView keyTest;

void showLog(Display &d)
{
    d.fillRect(0, 0, d.width(), d.height(), colors::Black);
    drawHeader(d);
    logView.draw(d, HeaderFont.height, true);
}

// A key, the knob, a signal or a transmission: the backlight comes on and
// stays on for its time.
void useLight(Display &d)
{
    lastLightUse = os::nowMs();
    if (!backlightLit) {
        backlightLit = true;
        d.setBacklight(backlightLevel);
        services::power::hold(services::power::AwakeReason::Display);
    }
}

void uiMain(void *)
{
    Display &d = *display;
    d.start();
    logView.configure(d.width() / LogFont.width, (d.height() - HeaderFont.height) / LogFont.height);
    drawStartScreen(d);
    d.setBacklight(DefaultBacklight);
    services::power::hold(services::power::AwakeReason::Display);
    lastLightUse = os::nowMs();
    log::info("ui") << "display " << d.width() << " x " << d.height();

    quickKeys << keyText(Key::Menu) << "  " << keyText(Key::Front1) << " Zone  " << keyText(Key::Front2) << " Chan  "
              << keyText(Key::Hash) << " VFO";

    // The codeplug: settings, and the channel the radio starts on.
    if (codeplug != nullptr) {
        static ChannelSelector selector(*codeplug);
        selector.setAdjust(takeKeptChannel);
        selector.load();
        if (selector.current()) {
            channels = &selector;
            keyLock.setSettings(selector.settings().keyLock);
            backlightLevel = selector.settings().backlightLevel;
            d.setBacklight(backlightLevel);
            backlightS = selector.settings().backlightS;
            batterySave = selector.settings().batterySave;
            if (selector.settings().voiceEnhancement) {
                radio::post(radio::VoiceEnhancement{true});
            }
            plugDmrMicGainDb =
                static_cast<std::uint8_t>(services::microphone_gain::fromLevel(selector.settings().dmrMicrophoneLevel));
            plugFmMicGainDb =
                static_cast<std::uint8_t>(services::microphone_gain::fromLevel(selector.settings().fmMicrophoneLevel));
            gpsAtStart = selector.settings().gps;
            gpsSystems = selector.settings().gpsSystems;
            timeZoneMinutes = selector.settings().timeZoneMinutes;
            const services::RoamingSettings &roaming = selector.settings().roaming;
            roamingZone = roaming.zone;
            autoRoaming = roaming.automatic;
            loadRoamingSites();
            radio::post(radio::RoamingSetup{roaming});
            std::copy(std::begin(selector.settings().sideShort), std::end(selector.settings().sideShort), sideShort);
            std::copy(std::begin(selector.settings().sideLong), std::end(selector.settings().sideLong), sideLong);
            log::info("ui") << "codeplug: zone " << selector.zone() << " \"" << selector.zoneName() << "\", "
                            << selector.zoneSize() << " channels" << (selector.vfoMode() ? ", VFO" : "");
            logLockSettings();
        } else {
            log::warning("ui") << "codeplug: no channel to start on";
        }
        if (const auto p = codeplug->preferences()) {
            homeLayout = p->home;
            dmrMicGainDb = p->dmrMicGainDb;
            fmMicGainDb = p->fmMicGainDb;
        }
        postMicrophoneGain();
    }

    View view = View::Start;
    const os::Ticks startUntil = os::nowMs() + StartScreenMs;
    bool startingShown = false;
    os::Ticks lastSecond = 0;
    os::Ticks noticeUntil = 0;
    bool noticeShown = false;
    bool volumeNoticeUp = false; // the notice up is the volume one
    constexpr os::Ticks NoticeMs = 1500;

    // The standby, log and key test screens take the lock; the zone list,
    // the test pattern and full-screen messages do not.
    auto lockContext = [&] {
        services::KeyLock::Context c;
        c.screenLockable = view == View::Standby || view == View::Gps || view == View::Log || view == View::KeyTest;
        c.transmitting = transmitting;
        c.nowMs = os::nowMs();
        return c;
    };
    auto notice = [&](std::string_view text) {
        volumeNoticeUp = false;
        drawNotice(d, text);
        noticeShown = true;
        noticeUntil = os::nowMs() + NoticeMs;
    };
    // The radio's chips are up and the start screen is gone.
    auto hardwareUp = [&] { return view != View::Start && radio::ready(); };
    // A key or knob turn refused because the chips are not up yet: on the
    // start screen silently, else with a notice once per press.
    auto refuseNotReady = [&](bool press) {
        if (view == View::Start || !press) {
            return;
        }
        log::info("ui") << "key refused: the radio is not ready";
        notice("Please wait...");
    };
    lib::StaticString<6> clockText;
    lib::StaticString<12> lastCallerText;
    // The home screen's live rows are checked this often.
    constexpr os::Ticks HomeCheckMs = 250;
    os::Ticks lastHomeCheck = 0;
    bool gpsScreenNeed = false;
    // Battery save also decides whether the GPS receiver sleeps between fixes.
    auto setBatterySave = [&](std::uint8_t mode) {
        batterySave = mode;
        radio::post(radio::BatterySave{mode});
        gps::post(gps::PowerSave{mode != 0});
    };
    auto standbyState = [&] {
        StandbyState s;
        s.transmitting = transmitting;
        s.receiving = receiving;
        s.locked = keyLock.locked() || keyLock.settings().forced;
        s.entry = std::string_view(entry, entryLength);
        s.keys = quickKeys.view();
        s.gps = gpsState;
        s.layout = homeLayout;
        services::HomeReadings &r = s.readings;
        clockText.clear();
        if (const auto utc = system::utcNow()) {
            clockText = localClockText(*utc);
            r.clockMinutes = (static_cast<std::int64_t>(*utc) + timeZoneMinutes * 60) / 60;
        }
        s.clock = clockText.view();
        r.volume = volumeLevel;
        r.centivolts = (batteryState.millivolts + 5) / 10;
        r.batteryLow = batteryState.low;
        const Channel *ch = channels != nullptr && channels->current() ? &*channels->current() : nullptr;
        s.roaming = ch != nullptr && ch->digital() && ch->roaming && roamingSites > 0;
        s.roamSearching = roam.searching;
        s.roamLost = roam.lost;
        s.roamHz = roam.receiveHz;
        s.roamColourCode = roam.colourCode;
        s.roamPosition = roam.site + 1;
        s.roamSite = roam.name;
        if (ch != nullptr && hasTalkgroup(*ch)) {
            nameTalkgroup(ch->dmr.contact);
            s.talkgroup = talkgroupText;
            s.talkgroupName = talkgroupName;
        }
        // The signal: a quiet roaming repeater's last level while it is
        // current, else the level heard in the last moments.
        const os::Ticks now = os::nowMs();
        if (homeLayout.showsSignal()) {
            if (s.roaming && !receiving && roam.site >= 0) {
                radio::roamingView(roamView);
                for (std::size_t i = 0; i < roamView.count; ++i) {
                    const auto &e = roamView.entries[i];
                    if (e.position == roam.site && e.current(now)) {
                        r.signalKnown = true;
                        r.signalDbm = e.levelDbm;
                    }
                }
            }
            const radio::SignalLevel level = radio::signalLevel();
            if (!r.signalKnown && level.known && now - level.atMs < SignalHoldMs) {
                r.signalKnown = true;
                r.signalDbm = level.dbm;
            }
        }
        if (lastCallAt != 0) {
            r.lastCallAt = lastCallAt;
            r.lastCallAge = services::ageOf((now - lastCallAt) / 1000);
            s.lastCaller = lastCaller;
            if (lastCaller[0] == '\0') {
                lastCallerText.clear();
                lastCallerText << lastCallerId;
                s.lastCaller = lastCallerText.view();
            }
        }
        if (gpsState.on && gpsState.fix.valid) {
            r.positionKnown = true;
            r.latitudeE5 = gpsState.fix.latitudeE7 / 100;
            r.longitudeE5 = gpsState.fix.longitudeE7 / 100;
        }
        return s;
    };
    // The home screen is up, not the call screen.
    auto homeShown = [&] { return view == View::Standby && !heard.shown && channels != nullptr; };
    auto callInfo = [&] {
        CallInfo c;
        c.active = heard.active;
        c.allCall = heard.destination == AllCallId;
        c.group = heard.group;
        c.callerName = heard.callerName;
        c.callerId = heard.source;
        c.targetName = heard.target;
        c.targetId = heard.destination;
        c.callerHeadline = heard.caller;
        const std::optional<Channel> ch = channels != nullptr ? channels->current() : std::nullopt;
        if (ch && ch->receiveHz != ch->transmitHz) {
            c.slot = ch->dmr.slot;
        }
        callChannel[0] = '\0';
        if (ch) {
            copyName(callChannel, ch->name);
        }
        c.channel = callChannel;
        c.seconds = static_cast<std::uint32_t>(((heard.active ? os::nowMs() : heard.ended) - heard.started) / 1000);
        return c;
    };
    auto barHeader = [&](bool bar) {
        headerGround = colors::Black;
        // Above the status row's rule, so the battery never covers it.
        headerHeight = bar ? StandbyScreen::StatusH - 1 : HeaderFont.height;
        headerTop = bar ? 2 : 0;
    };
    // The menu's settings as the radio has them now.
    auto menuSettings = [&] {
        menu::Settings s;
        s.squelch = channels->squelch();
        if (channels->current()) {
            s.squelchMode = channels->current()->squelchMode;
            s.power = channels->current()->power;
        }
        s.roaming = autoRoaming;
        s.roamZone = roamingZone;
        s.backlight = static_cast<std::uint8_t>(backlightLevel);
        s.backlightS = backlightS;
        s.keyLock = keyLock.settings();
        s.batterySave = batterySave;
        s.gps = gpsState.on;
        s.gpsSystems = gpsSystems;
        s.timeZoneMinutes = static_cast<std::int16_t>(timeZoneMinutes);
        s.home = homeLayout;
        s.dmrMicGainDb = dmrMicGainDb;
        s.fmMicGainDb = fmMicGainDb;
        s.plugDmrMicGainDb = plugDmrMicGainDb;
        s.plugFmMicGainDb = plugFmMicGainDb;
        return s;
    };
    // A row's value: the setting's, or the state of what the row opens.
    auto rowValue = [&](menu::Row row, const menu::Settings &s) {
        menu::ValueText t;
        if (row.opensGroup()) {
            return t;
        }
        switch (row.item) {
        case menu::Item::RoamStatus:
            radio::roamingView(roamView);
            t << roamStateText(roamView).view();
            break;
        case menu::Item::GpsStatus:
            t << (!gpsState.on ? "Off" : gpsState.asleep ? "Sleeping"
                    : gpsState.fix.valid                 ? "Fix"
                    : gpsState.receiving                 ? "Searching"
                                                         : "No data");
            break;
        case menu::Item::RoamZone: {
            std::array<char, CodeplugSource::ZoneNameBytes + 1> name{};
            t << (roamingZone != services::RoamingSettings::NoZone && codeplug->roamingZoneName(roamingZone, name)
                    ? std::string_view(name.data())
                    : std::string_view("-"));
            break;
        }
        default:
            if (menu::info(row.item).kind == menu::Kind::Value) {
                t = menu::valueText(row.item, menu::choiceOf(row.item, s));
            }
            break;
        }
        return t;
    };
    // The menu list: up to five rows around the one chosen, and the panel
    // about it.
    auto drawMenu = [&] {
        static ListRow rows[MenuRows];
        static menu::ValueText values[MenuRows];
        const menu::GroupInfo &g = menuNav.groupInfo();
        const std::size_t first = firstShown(menuNav.row(), g.rows.size(), MenuRows);
        const menu::Settings s = menuSettings();
        std::size_t n = 0;
        for (; n < std::size(rows) && first + n < g.rows.size(); ++n) {
            const menu::Row row = g.rows[first + n];
            values[n] = rowValue(row, s);
            rows[n] = {menu::label(row), values[n].view(), row.opensGroup() || menu::info(row.item).kind == menu::Kind::Screen};
        }
        std::string_view about1;
        std::string_view about2;
        lib::StaticString<24> uptime;
        if (menuNav.current().item == menu::Item::Version) {
            uptime << "up " << os::nowMs() / 60'000 << " min";
            about1 = Version;
            about2 = uptime.view();
        } else {
            menu::splitAbout(menu::about(menuNav.current()), about1, about2);
        }
        drawMenuScreen(d, menu::groupTitle(menuNav.group()).view(), std::span(rows, n), first, g.rows.size(), menuNav.row(),
            about1, about2, menuNav.hint());
    };
    auto drawMenuValue = [&] {
        drawValueScreen(d, menu::valueTitle(menuNav.group(), menuNav.current().item).view(), menuNav.choices(),
            menuNav.cursor(), menuNav.inUse(), menuNav.hint());
    };
    // The key hint under the zone list and the roaming status.
    auto listHint = [&]() -> std::string_view {
        return view == View::RoamStatus ? "Menu: search   Exit: back" : "Menu: use   Exit: back";
    };
    auto redrawView = [&] {
        // A full redraw covers any notice: the next one is drawn whole.
        noticeShown = false;
        volumeNoticeUp = false;
        const bool radioScreen =
            view == View::Standby || isList(view) || view == View::Gps;
        barHeader(radioScreen);
        if (view == View::Standby && heard.shown) {
            standby.drawCall(d, standbyState(), callInfo());
            drawBattery(d);
        } else if (view == View::Standby) {
            standby.draw(d, *channels, standbyState());
            drawBattery(d);
        } else if (view == View::Gps) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
            drawGpsScreen(d, gpsState, true);
        } else if (view == View::Menu) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
            if (menuNav.choosing()) {
                drawMenuValue();
            } else {
                drawMenu();
            }
        } else if (isList(view)) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
            zonePicker.draw(d, listHint());
        } else if (view == View::Log) {
            showLog(d);
        } else if (view == View::KeyTest) {
            keyTest.draw(d);
        }
    };
    auto show = [&](View v) {
        view = v;
        redrawView();
    };
    // The next screen in Exit-held order: standby, log, key test.
    auto nextView = [&] {
        if (view == View::Standby || view == View::Gps) {
            return View::Log;
        }
        if (view == View::Log) {
            return View::KeyTest;
        }
        return channels != nullptr ? View::Standby : View::Log;
    };
    using Verdict = services::KeyLock::Verdict;
    auto refreshLockMark = [&] {
        if (view == View::Log || view == View::KeyTest) {
            drawLockMark(d);
        } else if (view == View::Standby || isList(view) || view == View::Gps) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
        }
    };
    // Changes kept in the codeplug; the store writes them after a quiet time.
    auto keepSettings = [&] {
        if (settingsStore == nullptr || channels == nullptr) {
            return;
        }
        CodeplugSettings s = channels->settings();
        s.keyLock = keyLock.settings();
        s.batterySave = batterySave;
        s.backlightS = backlightS;
        if (backlightLevel >= 1) {
            s.backlightLevel = static_cast<std::uint8_t>(backlightLevel);
        }
        s.gps = gpsAtStart;
        s.gpsSystems = gpsSystems;
        s.timeZoneMinutes = static_cast<std::int16_t>(timeZoneMinutes);
        s.roaming.zone = roamingZone;
        s.roaming.automatic = autoRoaming;
        settingsStore->keepSettings(s, os::nowMs());
    };
    auto keepPreferences = [&] {
        if (settingsStore != nullptr) {
            services::RadioPreferences p;
            p.home = homeLayout;
            p.dmrMicGainDb = dmrMicGainDb;
            p.fmMicGainDb = fmMicGainDb;
            settingsStore->keepPreferences(p, os::nowMs());
        }
    };
    auto keepChannel = [&] {
        if (settingsStore != nullptr && channels != nullptr && channels->current() &&
            channels->channelIndex() != CodeplugSource::NoChannel) {
            const Channel &c = *channels->current();
            settingsStore->keepChannel(channels->channelIndex(), c.power, c.squelchMode, os::nowMs());
        }
    };
    auto useChannel = [&] {
        if (channels == nullptr || !channels->current()) {
            return;
        }
        const Channel &c = *channels->current();
        static std::uint32_t groups[CodeplugSource::MaxReceiveGroups];
        std::size_t n = 0;
        if (c.digital() && c.dmr.groupList != services::dmr::ChannelSettings::NoGroupList && codeplug != nullptr) {
            n = codeplug->receiveGroups(c.dmr.groupList, groups);
        }
        radio::setReceiveGroups(std::span(groups, n));
        heard.shown = false;
        radio::post(radio::UseChannel{c, channels->squelch()});
        if (view == View::Standby) {
            redrawView();
        }
        if (settingsStore != nullptr && !channels->vfoMode()) {
            settingsStore->keepZone(channels->sideB(), channels->zone(),
                static_cast<std::uint16_t>(channels->position() - 1), os::nowMs());
        }
    };
    auto tuneEntry = [&] {
        std::uint32_t khz = 0;
        for (std::size_t i = 0; i < EntryDigits; ++i) {
            khz = khz * 10 + static_cast<std::uint32_t>(i < entryLength ? entry[i] - '0' : 0);
        }
        entryLength = 0;
        channels->tune(khz * 1000);
        log::info("ui") << "VFO " << khz / 1000 << "." << khz % 1000 / 100 << khz % 100 / 10 << khz % 10 << " MHz";
        useChannel();
        redrawView();
    };
    auto openZonePicker = [&] {
        static std::uint8_t used[CodeplugSource::MaxZones];
        zonePicker.open("Zone");
        const std::size_t n = channels->zones(used);
        for (std::size_t i = 0; i < n; ++i) {
            char name[ChannelSelector::NameBytes];
            channels->zoneLabel(used[i], name);
            zonePicker.add(used[i], name);
        }
        zonePicker.select(channels->zone());
        show(View::ZonePicker);
    };
    // Puts the menu's settings @p s into use where they differ from the
    // radio's.
    auto useSettings = [&](const menu::Settings &s) {
        const menu::Settings was = menuSettings();
        bool channel = false;
        if (s.squelch != was.squelch) {
            channels->setSquelch(s.squelch);
            log::info("ui") << "squelch " << s.squelch;
            channel = true;
        }
        if (s.squelchMode != was.squelchMode) {
            channels->setSquelchMode(s.squelchMode);
            log::info("ui") << "squelch opens on " << (s.squelchMode == SquelchMode::Tone ? "CTCSS/DCS" : "the carrier");
            channel = true;
        }
        if (s.power != was.power) {
            channels->setPower(s.power);
            log::info("ui") << "power " << static_cast<int>(s.power);
            channel = true;
        }
        if (s.roaming != was.roaming) {
            autoRoaming = s.roaming;
            radio::post(radio::RoamAutomatic{autoRoaming});
            log::info("ui") << "auto roaming " << (autoRoaming ? "on" : "off");
        }
        if (s.roamZone != was.roamZone) {
            roamingZone = s.roamZone;
            loadRoamingSites();
            log::info("ui") << "roaming zone " << roamingZone << ", " << roamingSites << " repeaters";
            channel = true;
        }
        if (s.backlight != was.backlight) {
            backlightLevel = s.backlight;
            d.setBacklight(backlightLevel);
        }
        if (s.backlightS != was.backlightS) {
            backlightS = s.backlightS;
            log::info("ui") << "light time " << menu::valueText(menu::Item::BacklightTime, backlightS).view();
        }
        if (s.keyLock != was.keyLock) {
            keyLock.setSettings(s.keyLock);
            logLockSettings();
        }
        if (s.batterySave != was.batterySave) {
            setBatterySave(s.batterySave);
        }
        if (s.gps != was.gps) {
            // The menu shows the new value before the gps task reports it.
            gpsState.on = s.gps;
            gpsAtStart = s.gps;
            gps::post(gps::Power{s.gps});
        }
        if (s.gpsSystems != was.gpsSystems) {
            gpsSystems = s.gpsSystems;
            gps::post(gps::Systems{gpsSystems});
        }
        if (s.timeZoneMinutes != was.timeZoneMinutes) {
            timeZoneMinutes = s.timeZoneMinutes;
            log::info("ui") << "time zone " << menu::valueText(menu::Item::TimeZone, timeZoneMinutes).view();
        }
        if (s.home != was.home) {
            homeLayout = s.home;
            log::info("ui") << "home screen "
                            << menu::valueText(menu::Item::HomePreset, menu::choiceOf(menu::Item::HomePreset, s)).view();
        }
        if (s.dmrMicGainDb != was.dmrMicGainDb || s.fmMicGainDb != was.fmMicGainDb) {
            dmrMicGainDb = s.dmrMicGainDb;
            fmMicGainDb = s.fmMicGainDb;
            postMicrophoneGain();
        }
        if (channel) {
            useChannel();
        }
    };
    // The value chosen in the menu takes effect at once and is kept in the
    // codeplug.
    auto applyMenuValue = [&](menu::Item item, std::int32_t value) {
        menu::Settings s = menuSettings();
        menu::setChoice(item, value, s);
        useSettings(s);
        switch (menu::keptIn(item)) {
        case menu::Kept::Settings:
            keepSettings();
            break;
        case menu::Kept::Preferences:
            keepPreferences();
            break;
        case menu::Kept::Channel:
            keepChannel();
            break;
        case menu::Kept::No:
            break;
        }
    };
    // Lists the choices that come from the codeplug.
    auto listCodeplugChoices = [&](menu::Item item, menu::Choices &out) {
        if (item != menu::Item::RoamZone) {
            return;
        }
        std::uint8_t zones[CodeplugSource::MaxRoamingZones];
        const std::size_t n = codeplug->roamingZones(zones);
        for (std::size_t i = 0; i < n; ++i) {
            std::array<char, CodeplugSource::ZoneNameBytes + 1> name{};
            codeplug->roamingZoneName(zones[i], name);
            out.add(zones[i], name.data());
        }
        if (n == 0) {
            out.add(services::RoamingSettings::NoZone, "None");
        }
    };
    // The screen a menu row opens.
    auto showItem = [&](menu::Item item) {
        switch (item) {
        case menu::Item::RoamStatus:
            zonePicker.open("Roaming");
            fillRoamStatus(zonePicker);
            show(View::RoamStatus);
            break;
        case menu::Item::GpsStatus:
            show(View::Gps);
            break;
        case menu::Item::Log:
            show(View::Log);
            break;
        case menu::Item::KeyTest:
            show(View::KeyTest);
            break;
        default:
            break;
        }
    };
    // Carries out what a key or the knob did in the menu.
    auto menuStep = [&](menu::Navigator::Step step) {
        using Step = menu::Navigator::Step;
        switch (step) {
        case Step::None:
            break;
        case Step::Moved:
            if (menuNav.choosing()) {
                drawMenuValue();
            } else {
                drawMenu();
            }
            break;
        case Step::Opened:
            show(View::Menu);
            break;
        case Step::Choose: {
            const menu::Item item = menuNav.current().item;
            if (!menu::listChoices(item, menuNav.choices(), menuSettings())) {
                listCodeplugChoices(item, menuNav.choices());
            }
            menuNav.choose(menu::choiceOf(item, menuSettings()));
            show(View::Menu);
            break;
        }
        case Step::Set:
            applyMenuValue(menuNav.current().item, menuNav.chosen());
            show(menuNav.isOpen() ? View::Menu : View::Standby);
            break;
        case Step::Show:
            showItem(menuNav.current().item);
            break;
        case Step::Left:
            show(View::Standby);
            break;
        }
    };
    // A key on the standby screen or the zone list, after the lock let it
    // through.
    auto channelKey = [&](Key key, KeyDebouncer::EventType type) {
        using Type = KeyDebouncer::EventType;
        const bool press = type == Type::Press;
        const bool step = press || type == Type::Repeat;
        if (view == View::Menu) {
            if ((step && (key == Key::Up || key == Key::Down)) || (press && (key == Key::Menu || key == Key::Exit))) {
                menuStep(menuNav.press(key));
            }
            return;
        }
        if (view == View::RoamStatus) {
            if (step && (key == Key::Up || key == Key::Down)) {
                zonePicker.move(key == Key::Up ? -1 : 1);
                zonePicker.draw(d, listHint());
            } else if (press && key == Key::Menu && zonePicker.chosen() == SearchNowRow) {
                radio::post(radio::RoamNow{});
            } else if (press && key == Key::Exit) {
                show(View::Menu);
            }
            return;
        }
        if (view == View::ZonePicker) {
            if (step && (key == Key::Up || key == Key::Down)) {
                zonePicker.move(key == Key::Up ? -1 : 1);
                zonePicker.draw(d, listHint());
            } else if (press && (key == Key::Menu || key == Key::Front1)) {
                channels->selectZone(zonePicker.chosen());
                show(View::Standby);
                useChannel();
            } else if (press && key == Key::Exit) {
                show(View::Standby);
            }
            return;
        }
        // A key on the call screen goes back to standby; Exit does only that.
        if (press && heard.shown) {
            heard.shown = false;
            redrawView();
            if (key == Key::Exit) {
                return;
            }
        }
        if (step && (key == Key::Up || key == Key::Down)) {
            entryLength = 0;
            const int dir = key == Key::Up ? 1 : -1;
            if (channels->vfoMode()) {
                channels->tune(channels->current()->receiveHz + static_cast<std::uint32_t>(dir * VfoStepHz));
            } else {
                channels->step(dir);
            }
            useChannel();
            redrawView();
        } else if (press && key >= Key::Digit0 && key <= Key::Digit9) {
            if (entryLength < EntryDigits) {
                entry[entryLength++] = static_cast<char>('0' + static_cast<int>(key) - static_cast<int>(Key::Digit0));
            }
            if (entryLength == EntryDigits) {
                tuneEntry();
            } else {
                redrawView();
            }
        } else if (press && key == Key::Menu && entryLength > 0) {
            tuneEntry();
        } else if (press && key == Key::Menu) {
            menuNav.open();
            show(View::Menu);
        } else if (press && key == Key::Exit && entryLength > 0) {
            entryLength = 0;
            redrawView();
        } else if (press && key == Key::Hash) {
            entryLength = 0;
            if (channels->vfoMode()) {
                channels->memoryMode();
            } else {
                channels->tune(channels->current()->receiveHz);
            }
            useChannel();
            redrawView();
        } else if (press && key == Key::Front1) {
            entryLength = 0;
            openZonePicker();
        } else if (press && key == Key::Front2) {
            entryLength = 0;
            menuNav.open(menu::Group::Channel, true);
            show(View::Menu);
        }
    };
    // A side key's function: on the press when it has no long-press
    // function, else on the release before a long press.
    auto sideKey = [&](Key key, KeyDebouncer::EventType type) {
        using Type = KeyDebouncer::EventType;
        const std::size_t k = static_cast<std::size_t>(key) - static_cast<std::size_t>(Key::Side1);
        services::KeyFunction f = services::KeyFunction::None;
        if (type == Type::Press) {
            sideLongDone[k] = false;
            f = sideLong[k] == services::KeyFunction::None ? sideShort[k] : services::KeyFunction::None;
        } else if (type == Type::LongPress) {
            sideLongDone[k] = true;
            f = sideLong[k];
        } else if (type == Type::Release && sideLong[k] != services::KeyFunction::None && !sideLongDone[k]) {
            f = sideShort[k];
        }
        if (f == services::KeyFunction::Roam) {
            radio::post(radio::RoamNow{});
        }
    };
    auto onLockVerdict = [&](Verdict v) {
        if (v == Verdict::Locked || v == Verdict::Unlocked) {
            refreshLockMark();
        }
        switch (v) {
        case Verdict::Pass:
        case Verdict::Ignored:
            break;
        case Verdict::Refused:
            audio::post(audio::Beep{audio::Sound::Refused});
            notice("Keys locked");
            break;
        case Verdict::Locked:
            audio::post(audio::Beep{audio::Sound::Locked});
            log::info("ui") << "keys locked";
            notice("Keys locked");
            break;
        case Verdict::UnlockPrompt:
            notice("* to unlock");
            break;
        case Verdict::Unlocked:
            audio::post(audio::Beep{audio::Sound::Unlocked});
            log::info("ui") << "keys unlocked";
            notice("Keys unlocked");
            break;
        }
    };

    while (!os::stopRequested()) {
        if (settingsStore != nullptr) {
            const services::SaveResult saved = settingsStore->flush(os::nowMs());
            if (saved == services::SaveResult::Saved) {
                log::info("ui") << "settings kept in the codeplug";
            } else if (saved == services::SaveResult::Busy) {
                log::info("ui") << "settings wait: the PC is programming";
            } else if (saved == services::SaveResult::Failed) {
                log::warning("ui") << "settings not kept: the codeplug write failed";
            }
        }
        Message m;
        if (inbox.receive(m, RedrawStepMs)) {
            auto refreshValues = [&] {
                if (view == View::KeyTest) {
                    keyTest.drawValues(d);
                }
            };
            lib::visit(lib::Overload{
                           [&](const Backlight &b) {
                               backlightLevel = b.level;
                               d.setBacklight(b.level);
                               backlightLit = true;
                               lastLightUse = os::nowMs();
                               services::power::hold(services::power::AwakeReason::Display);
                               log::info("ui") << "backlight " << b.level;
                           },
                           [&](const BacklightTime &t) {
                               backlightS = t.seconds;
                               useLight(d);
                               log::info("ui") << "light time " << menu::valueText(menu::Item::BacklightTime, backlightS).view();
                               keepSettings();
                           },
                           [&](const ShowTestPattern &) {
                               drawTestPattern(d);
                               view = View::TestPattern;
                           },
                           [&](const ShowLog &) { show(View::Log); },
                           [&](const ShowKeyTest &) { show(View::KeyTest); },
                           [&](const ToggleView &) { show(nextView()); },
                           [&](const PanelPower &p) { d.setPanelOn(p.on); },
                           [&](const Keys &k) { keyTest.setKeys(d, k.mask, view == View::KeyTest); },
                           [&](const KeyEvent &e) {
                               using Type = KeyDebouncer::EventType;
                               useLight(d);
                               // Until the radio's chips are up, at start-up or while one
                               // restarts, every key is refused: nothing may reach them
                               // half set up. A PTT release still goes through.
                               if (!hardwareUp() && !(e.key == Key::Ptt && e.type == Type::Release)) {
                                   refuseNotReady(e.type == Type::Press);
                                   return;
                               }
                               if (e.key == Key::Ptt) {
                                   // Press and release only; a refused press never keys
                                   // the radio, a release always reaches it.
                                   if (e.type == Type::Press || e.type == Type::Release) {
                                       const bool down = e.type == Type::Press;
                                       const Verdict v = keyLock.ptt(down, lockContext());
                                       if (v == Verdict::Pass) {
                                           radio::post(radio::Ptt{down});
                                       } else {
                                           log::info("ui") << "PTT refused: keys locked";
                                           onLockVerdict(v);
                                       }
                                   }
                                   return;
                               }
                               const Verdict v = keyLock.key(e.key, e.type, lockContext());
                               if (v != Verdict::Pass) {
                                   // Repeats of a refused key stay quiet.
                                   if (e.type != Type::Repeat) {
                                       onLockVerdict(v);
                                   }
                                   return;
                               }
                               if (e.type == Type::Press) {
                                   audio::post(audio::Beep{audio::Sound::Key});
                               }
                               if (e.key == Key::Exit && e.type == Type::LongPress) {
                                   entryLength = 0;
                                   show(nextView());
                                   return;
                               }
                               if (e.key >= Key::Side1 && e.key <= Key::Side3) {
                                   if (channels != nullptr) {
                                       sideKey(e.key, e.type);
                                   }
                                   return;
                               }
                               if (view == View::Gps) {
                                   if (e.type == Type::Press && (e.key == Key::Exit || e.key == Key::Menu)) {
                                       show(View::Menu);
                                   }
                                   return;
                               }
                               if (channels != nullptr && (view == View::Standby || isList(view))) {
                                   channelKey(e.key, e.type);
                               }
                           },
                           [&](const Transmitting &t) {
                               transmitting = t.on;
                               useLight(d);
                               if (view == View::Log) {
                                   drawHeader(d);
                               }
                               refreshLockMark();
                           },
                           [&](const Receiving &r) {
                               receiving = r.on;
                               useLight(d);
                               refreshLockMark();
                           },
                           [&](const DmrCall &c) {
                               if (c.active) {
                                   heard.shown = true;
                                   heard.active = true;
                                   heard.group = c.group;
                                   heard.source = c.source;
                                   heard.destination = c.destination;
                                   heard.started = os::nowMs();
                                   nameCall(heard, codeplug);
                                   lastLightUse = os::nowMs();
                               } else if (heard.active) {
                                   heard.active = false;
                                   heard.ended = os::nowMs();
                                   copyName(lastCaller, heard.caller);
                                   lastCallerId = heard.source;
                                   lastCallAt = heard.ended;
                               }
                               if (view == View::Standby && !noticeShown) {
                                   redrawView();
                               }
                           },
                           [&](const Roaming &r) {
                               roam.searching = r.searching;
                               roam.lost = r.lost;
                               roam.site = r.site;
                               roam.receiveHz = r.receiveHz;
                               roam.colourCode = r.colourCode;
                               roam.name[0] = '\0';
                               std::array<char, CodeplugSource::SiteNameBytes + 1> name{};
                               if (r.site >= 0 && codeplug != nullptr &&
                                   codeplug->roamingSiteName(roamingZone, static_cast<std::size_t>(r.site), name)) {
                                   copyName(roam.name, name.data());
                               }
                               if (view == View::Standby && !heard.shown && !noticeShown) {
                                   redrawView();
                               }
                               // The Roaming group's status value.
                               if (view == View::Menu && !menuNav.choosing() && menuNav.group() == menu::Group::Roaming &&
                                   !noticeShown) {
                                   drawMenu();
                               }
                           },
                           [&](const Notice &n) {
                               if (view != View::Start) {
                                   notice(n.text);
                               }
                           },
                           [&](const ShowStandby &) {
                               if (channels != nullptr) {
                                   show(View::Standby);
                               }
                           },
                           [&](const SetKeyLock &s) {
                               keyLock.setLocked(s.locked);
                               refreshLockMark();
                               logLockSettings();
                               notice(s.locked ? "Keys locked" : "Keys unlocked");
                           },
                           [&](const BatterySave &b) {
                               setBatterySave(b.mode);
                               keepSettings();
                           },
                           [&](const SetAutoRoaming &r) {
                               autoRoaming = r.on;
                               radio::post(radio::RoamAutomatic{autoRoaming});
                               keepSettings();
                           },
                           [&](const MicrophoneGain &g) {
                               auto set = [](std::int8_t db, std::optional<std::uint8_t> &kept) {
                                   if (db == MicrophoneGain::Unchanged) {
                                       return false;
                                   }
                                   kept = db == MicrophoneGain::FromCodeplug ? std::nullopt
                                                                             : std::optional(static_cast<std::uint8_t>(db));
                                   return true;
                               };
                               const bool dmr = set(g.dmrDb, dmrMicGainDb);
                               if (set(g.fmDb, fmMicGainDb) || dmr) {
                                   postMicrophoneGain();
                                   keepPreferences();
                               }
                               logMicrophoneGain();
                           },
                           [&](const KeyLockSettings &s) {
                               keyLock.setSettings(s.settings);
                               refreshLockMark();
                               logLockSettings();
                           },
                           [&](const KeyLockReport &) { logLockSettings(); },
                           [&](const SelectChannel &s) {
                               // Debug feature.
                               if (channels == nullptr) {
                                   return;
                               }
                               bool ok = !channels->vfoMode() || channels->memoryMode();
                               if (ok && s.zone != SelectChannel::KeepZone) {
                                   ok = channels->selectZone(s.zone);
                               }
                               if (ok && s.position > 0) {
                                   ok = s.position <= channels->zoneSize() &&
                                       channels->step(static_cast<int>(s.position) - static_cast<int>(channels->position()));
                               }
                               if (s.zone != SelectChannel::KeepZone || s.position > 0) {
                                   heard.shown = false;
                                   show(View::Standby);
                                   useChannel();
                               }
                               const auto &c = channels->current();
                               log::info("ui") << (ok ? "" : "not found; ") << "zone " << static_cast<int>(channels->zone()) << " ("
                                               << channels->zoneName() << "), channel " << channels->position() << " of "
                                               << channels->zoneSize() << ": " << (c ? c->name : "-") << ", "
                                               << (c ? c->receiveHz / 1000 : 0) << " kHz"
                                               << (c && c->roaming ? ", roaming" : "");
                           },
                           [&](const SettingsReport &) {
                               if (settingsStore != nullptr) {
                                   settingsStore->report();
                               }
                               if (codeplug == nullptr) {
                                   return;
                               }
                               // As the codeplug holds them now.
                               if (const auto s = codeplug->settings()) {
                                   log::info("ui") << "codeplug: squelch " << s->squelchA << '/' << s->squelchB << ", zone "
                                                   << s->zoneA << '/' << s->zoneB << ", light " << s->backlightLevel << ' '
                                                   << menu::valueText(menu::Item::BacklightTime, s->backlightS).view() << ", battery save "
                                                   << s->batterySave << ", auto lock " << s->keyLock.autoLockMs / 1000
                                                   << " s, GPS " << (s->gps ? "on" : "off") << " systems "
                                                   << s->gpsSystems << ", " << menu::valueText(menu::Item::TimeZone, s->timeZoneMinutes).view()
                                                   << ", auto roaming " << (s->roaming.automatic ? "on" : "off")
                                                   << ", roaming zone " << s->roaming.zone;
                               }
                               if (const auto p = codeplug->preferences()) {
                                   const services::HomeLayout &h = p->home;
                                   log::info("ui") << "preferences record: home screen " << static_cast<int>(h.top) << ' '
                                                   << (h.clock ? 1 : 0) << ' ' << static_cast<int>(h.big) << ' '
                                                   << static_cast<int>(h.middle) << ' ' << static_cast<int>(h.beside) << ' '
                                                   << static_cast<int>(h.bottom) << ", mic gain DMR "
                                                   << keptGainText(p->dmrMicGainDb).view() << ", FM "
                                                   << keptGainText(p->fmMicGainDb).view();
                               } else {
                                   log::info("ui") << "no preferences record";
                               }
                           },
                           [&](const EncoderClicks &c) {
                               useLight(d);
                               if (!hardwareUp()) {
                                   refuseNotReady(true);
                                   return;
                               }
                               const Verdict v = keyLock.knob(lockContext());
                               if (v != Verdict::Pass) {
                                   onLockVerdict(v);
                                   return;
                               }
                               static std::int32_t lastClicks = 0;
                               const std::int32_t moved = c.clicks - lastClicks;
                               lastClicks = c.clicks;
                               keyTest.setClicks(c.clicks);
                               refreshValues();
                               if (channels != nullptr && moved != 0) {
                                   if (view == View::Menu) {
                                       menuStep(menuNav.turn(moved));
                                   } else if (isList(view)) {
                                       zonePicker.move(moved);
                                       zonePicker.draw(d, listHint());
                                   } else if (view == View::Standby) {
                                       entryLength = 0;
                                       if (channels->vfoMode()) {
                                           channels->tune(channels->current()->receiveHz +
                                               static_cast<std::uint32_t>(moved * static_cast<int>(VfoStepHz)));
                                       } else {
                                           channels->step(moved);
                                       }
                                       useChannel();
                                   }
                               }
                           },
                           [&](const EncoderRaw &r) {
                               keyTest.setRaw(r.raw);
                               refreshValues();
                           },
                           [&](const Volume &v) {
                               keyTest.setVolume(v.permille);
                               refreshValues();
                               audio::post(audio::VolumeKnob{v.permille});
                               const int step = services::volumeStep(v.permille);
                               const bool changed = step != volumeLevel;
                               if (changed && volumeLevel >= 0) {
                                   useLight(d);
                               }
                               volumeLevel = step;
                               if (changed && view == View::Standby) {
                                   // Only what changes: the notice's number and
                                   // bar once it is up.
                                   drawVolumeNotice(d, step, !(noticeShown && volumeNoticeUp));
                                   noticeShown = true;
                                   volumeNoticeUp = true;
                                   noticeUntil = os::nowMs() + NoticeMs;
                               }
                           },
                           [&](const Battery &b) {
                               // The icon only when its fill or colour changes.
                               const BatteryState before = batteryState;
                               batteryState = {b.millivolts, b.level, b.low};
                               const bool iconChanged = b.level != before.level || b.low != before.low ||
                                   (b.millivolts > 0) != (before.millivolts > 0);
                               if (iconChanged && view != View::Start && view != View::TestPattern &&
                                   view != View::Message) {
                                   drawBattery(d);
                               }
                           },
                           [&](const Gps &g) {
                               const bool tagChanged = g.on != gpsState.on || g.receiving != gpsState.receiving ||
                                   g.fix.valid != gpsState.fix.valid || g.asleep != gpsState.asleep;
                               gpsState = {g.on, g.receiving, g.fix, g.asleep};
                               if (view == View::Gps) {
                                   if (tagChanged) {
                                       standby.drawStatus(d, standbyState());
                                       drawBattery(d);
                                   }
                                   if (!noticeShown) {
                                       drawGpsScreen(d, gpsState, false);
                                   }
                               } else if (tagChanged) {
                                   refreshLockMark();
                                   // The GPS group's status value.
                                   if (view == View::Menu && !menuNav.choosing() && menuNav.group() == menu::Group::Gps &&
                                       !noticeShown) {
                                       drawMenu();
                                   }
                               }
                           },
                           [&](const SwitchingOff &) {
                               view = View::Message;
                               d.fillRect(0, 0, d.width(), d.height(), colors::Black);
                               drawText(d, fonts::Spleen8x16, (d.width() - 14 * 8) / 2, (d.height() - 16) / 2, "Switching off",
                                   colors::White, colors::Black);
                           },
                       },
                m);
        }
        if (keyLock.update(lockContext())) {
            audio::post(audio::Beep{audio::Sound::Locked});
            log::info("ui") << "keys locked (auto lock)";
            refreshLockMark();
            notice("Keys locked");
        }
        if (noticeShown && static_cast<std::int32_t>(os::nowMs() - noticeUntil) >= 0) {
            noticeShown = false;
            volumeNoticeUp = false;
            redrawView();
        }
        // The start screen stays until the radio is ready, and at least its
        // own time.
        const bool startShown = static_cast<std::int32_t>(os::nowMs() - startUntil) >= 0;
        if (view == View::Start && startShown && !radio::ready() && !startingShown) {
            startingShown = true;
            drawStarting(d);
        }
        if (view == View::Start && startShown && radio::ready()) {
            show(channels != nullptr ? View::Standby : View::KeyTest);
            setBatterySave(batterySave);
            gps::post(gps::Systems{gpsSystems});
            if (gpsAtStart) {
                gps::post(gps::Power{true});
            }
            useChannel();
        }
        // The backlight goes off after its time, never during a signal or
        // a transmission.
        if (backlightLit && backlightS != 0 && !transmitting && !receiving && view != View::Message &&
            os::nowMs() - lastLightUse >= static_cast<os::Ticks>(backlightS) * 1000) {
            backlightLit = false;
            d.setBacklight(0);
            services::power::release(services::power::AwakeReason::Display);
        }
        // The GPS screen wants a position every second while open.
        if ((view == View::Gps) != gpsScreenNeed) {
            gpsScreenNeed = view == View::Gps;
            gps::post(gps::Need{services::GpsSchedule::Client::Screen, gpsScreenNeed ? 1000u : 0u});
        }
        // The home screen's rows that change by themselves: only those whose
        // values changed are drawn again.
        if (homeShown() && !noticeShown && os::nowMs() - lastHomeCheck >= HomeCheckMs) {
            lastHomeCheck = os::nowMs();
            standby.refresh(d, *channels, standbyState());
        }
        // The call screen: its time each second, back to standby a moment
        // after the call.
        if (view == View::Standby && heard.shown && !noticeShown) {
            const os::Ticks second = os::nowMs() / 1000;
            if (!heard.active && os::nowMs() - heard.ended >= CallHoldMs) {
                heard.shown = false;
                redrawView();
            } else if (heard.active && second != lastSecond) {
                lastSecond = second;
                standby.drawCallTime(d, callInfo());
            }
        }
        if (view == View::RoamStatus && !noticeShown) {
            const os::Ticks second = os::nowMs() / 1000;
            if (second != lastSecond) {
                lastSecond = second;
                fillRoamStatus(zonePicker);
                zonePicker.draw(d, listHint());
            }
        }
        if (view == View::Log) {
            const os::Ticks second = os::nowMs() / 1000;
            if (second != lastSecond) {
                lastSecond = second;
                drawHeader(d);
            }
            if (!noticeShown) {
                logView.draw(d, HeaderFont.height, false);
            }
        }
    }
}

} // namespace

void start(Display &d, CodeplugSource *c, const board::Input *i, board::SettingsStore *k)
{
    display = &d;
    codeplug = c;
    input = i;
    settingsStore = k;
    log::setTap(onLogLine);
    uiTask.start("ui", 4, uiMain, nullptr);
}

bool post(const Message &message)
{
    if (display == nullptr) {
        return false;
    }
    return inbox.send(message);
}

} // namespace dmm::app::ui
