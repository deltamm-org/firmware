// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "ui.h"

#include "audio.h"
#include "radio.h"
#include "standby.h"

#include "dmm/lib/variant.h"
#include "dmm/os/os.h"
#include "dmm/services/channel_selector.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/input.h"
#include "dmm/services/log.h"
#include "dmm/services/power.h"
#include "dmm/version.h"

#include <iterator>
#include <string_view>

namespace dmm::app::ui {

namespace {

using namespace services;
using services::log::Level;
namespace log = services::log;

os::Task<640> uiTask;
os::Queue<Message, 16> inbox;
Display *display = nullptr;
CodeplugSource *codeplug = nullptr;
const board::Input *input = nullptr;

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

// Right-aligned in the header: four bars (red when low) and the voltage.
// The radio's own screens put it in their taller status bar.
int headerHeight = 8;
int headerTop = 0;

void drawBattery(Display &d)
{
    const BatteryState &b = batteryState;
    const int barsX = d.width() - 13;
    lib::StaticString<12> text;
    if (b.millivolts > 0) {
        const int cv = (b.millivolts + 5) / 10;
        text << cv / 100 << '.' << static_cast<char>('0' + cv / 10 % 10) << 'V';
    }
    const int textW = static_cast<int>(text.view().size()) * HeaderFont.width;
    const int x0 = barsX - textW - 2;
    d.fillRect(x0, 0, d.width() - x0, headerHeight, headerGround);
    drawText(d, HeaderFont, x0, headerTop, text.view(), b.low ? colors::Red : colors::White, headerGround);
    if (b.millivolts <= 0) {
        return;
    }
    const Color on = b.low ? colors::Red : colors::Green;
    for (int i = 0; i < 4; ++i) {
        // The first bar always shows, so an empty battery still has one red bar.
        const bool lit = i == 0 || i <= b.level;
        d.fillRect(barsX + i * 3, headerTop + 7 - (i + 1) * 2 + 1, 2, (i + 1) * 2 - 1, lit ? on : rgb(60, 60, 60));
    }
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

// A short notice in a band across the middle of the screen.
void drawNotice(Display &d, std::string_view text)
{
    const Font &font = fonts::Spleen8x16;
    const int h = font.height + 10;
    const int y = (d.height() - h) / 2;
    d.fillRect(0, y, d.width(), h, palette::Ground);
    d.fillRect(0, y, d.width(), 1, palette::Rule);
    d.fillRect(0, y + h - 1, d.width(), 1, palette::Rule);
    const int w = static_cast<int>(text.size()) * font.width;
    drawText(d, font, (d.width() - w) / 2, y + 5, text, palette::Text, palette::Ground);
}

// The volume while the knob turns: "Volume" and a 15-segment bar.
void drawVolumeNotice(Display &d, int step)
{
    const Font &font = fonts::Spleen6x12;
    const int h = font.height + 16;
    const int y = (d.height() - h) / 2;
    d.fillRect(0, y, d.width(), h, palette::Ground);
    d.fillRect(0, y, d.width(), 1, palette::Rule);
    d.fillRect(0, y + h - 1, d.width(), 1, palette::Rule);
    lib::StaticString<12> label;
    label << "Volume " << step;
    drawText(d, font, 6, y + 3, label.view(), palette::Text, palette::Ground);
    constexpr int Segments = 15;
    const int segW = (d.width() - 12) / Segments;
    for (int i = 0; i < Segments; ++i) {
        d.fillRect(6 + i * segW, y + font.height + 6, segW - 2, 5, i < step ? palette::Text : palette::Rule);
    }
}

void logLockSettings()
{
    const services::KeyLockSettings &s = keyLock.settings();
    log::info("ui") << "key lock " << (keyLock.locked() ? "on" : "off") << "; covers" << (s.knob ? " knob" : "")
                    << (s.keypad ? " keypad" : "") << (s.side ? " side-keys+PTT" : "") << (s.forced ? ", forced" : "")
                    << ", auto " << s.autoLockMs / 1000 << " s";
}

// The start screen: the name and version on black.
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
    Menu,       // the settings list
    MenuValue,  // choosing one setting's value
    Log,
    KeyTest,
    Message,
};

// The radio's working channel, from the codeplug; nullptr without one.
ChannelSelector *channels = nullptr;
StandbyScreen standby;
ListPicker zonePicker;
ListPicker menu;
ListPicker menuValue;

// The settings menu: one flat list, each item a list of values.
enum class MenuItem : std::uint8_t
{
    Squelch,
    SquelchMode,
    Power,
    Backlight,
    BacklightTime,
    BatterySave,
    KeyLock,
    AutoLock,
    Count,
};
MenuItem menuItem = MenuItem::Squelch;

// Menu groups and their items. Settings of the current channel come first
// and also open directly from the standby screen (Front2).
enum class MenuGroup : std::uint8_t
{
    Channel,
    Display,
    Keys,
    Battery,
    Count,
};
struct GroupInfo
{
    const char *name;
    MenuItem items[4];
    std::uint8_t count;
};
constexpr GroupInfo Groups[] = {
    {"Channel", {MenuItem::Squelch, MenuItem::SquelchMode, MenuItem::Power}, 3},
    {"Display", {MenuItem::Backlight, MenuItem::BacklightTime}, 2},
    {"Keys", {MenuItem::KeyLock, MenuItem::AutoLock}, 2},
    {"Battery", {MenuItem::BatterySave}, 1},
};
static_assert(std::size(Groups) == static_cast<std::size_t>(MenuGroup::Count));
MenuGroup menuGroup = MenuGroup::Channel;
// Showing the group list (true) or one group's items.
bool menuAtGroups = true;
// The items were opened straight from the standby screen: leaving them
// returns there.
bool menuQuick = false;
int backlightLevel = DefaultBacklight;
// Backlight time choices, in seconds.
constexpr std::uint16_t BacklightTimes[] = {0, 5, 10, 15, 20, 25, 30, 60, 120, 180, 240, 300, 900, 1800, 2700, 3600};
std::uint16_t backlightS = 0; // 0 = always on
bool backlightLit = true;
os::Ticks lastLightUse = 0;
std::uint8_t batterySave = 0;
// The volume step 0..15, -1 until the knob has been read.
int volumeLevel = -1;
bool receiving = false;
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

lib::StaticString<12> durationText(std::uint16_t seconds)
{
    lib::StaticString<12> text;
    if (seconds == 0) {
        text << "Always";
    } else if (seconds >= 3600) {
        text << seconds / 3600 << " h";
    } else if (seconds >= 60) {
        text << seconds / 60 << " min";
    } else {
        text << seconds << " s";
    }
    return text;
}

const char *batterySaveText(std::uint8_t mode)
{
    constexpr const char *Texts[] = {"Off", "On", "Fast wake"};
    return Texts[mode <= 2 ? mode : 1];
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
        selector.load();
        if (selector.current()) {
            channels = &selector;
            keyLock.setSettings(selector.settings().keyLock);
            backlightS = selector.settings().backlightS;
            batterySave = selector.settings().batterySave;
            log::info("ui") << "codeplug: zone " << selector.zone() << " \"" << selector.zoneName() << "\", "
                            << selector.zoneSize() << " channels" << (selector.vfoMode() ? ", VFO" : "");
            logLockSettings();
        } else {
            log::warning("ui") << "codeplug: no channel to start on";
        }
    }

    View view = View::Start;
    const os::Ticks startUntil = os::nowMs() + StartScreenMs;
    os::Ticks lastSecond = 0;
    os::Ticks noticeUntil = 0;
    bool noticeShown = false;
    constexpr os::Ticks NoticeMs = 1500;

    // The standby, log and key test screens take the lock; the zone list,
    // the test pattern and full-screen messages do not.
    auto lockContext = [&] {
        services::KeyLock::Context c;
        c.screenLockable = view == View::Standby || view == View::Log || view == View::KeyTest;
        c.transmitting = transmitting;
        c.nowMs = os::nowMs();
        return c;
    };
    auto notice = [&](std::string_view text) {
        drawNotice(d, text);
        noticeShown = true;
        noticeUntil = os::nowMs() + NoticeMs;
    };
    auto standbyState = [&] {
        StandbyState s;
        s.transmitting = transmitting;
        s.receiving = receiving;
        s.locked = keyLock.locked() || keyLock.settings().forced;
        s.entry = std::string_view(entry, entryLength);
        s.volume = volumeLevel;
        s.keys = quickKeys.view();
        return s;
    };
    auto barHeader = [&](bool bar) {
        headerGround = colors::Black;
        // Above the status row's rule, so the battery never covers it.
        headerHeight = bar ? StandbyScreen::StatusH - 1 : HeaderFont.height;
        headerTop = bar ? 2 : 0;
    };
    auto redrawView = [&] {
        const bool radioScreen =
            view == View::Standby || view == View::ZonePicker || view == View::Menu || view == View::MenuValue;
        barHeader(radioScreen);
        if (view == View::Standby) {
            standby.draw(d, *channels, standbyState());
            drawBattery(d);
        } else if (view == View::ZonePicker || view == View::Menu || view == View::MenuValue) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
            ListPicker &list = view == View::ZonePicker ? zonePicker : view == View::Menu ? menu
                                                                                          : menuValue;
            list.draw(d, StandbyScreen::StatusH);
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
        if (view == View::Standby) {
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
        } else if (view == View::Standby || view == View::ZonePicker || view == View::Menu || view == View::MenuValue) {
            standby.drawStatus(d, standbyState());
            drawBattery(d);
        }
    };
    // One item's label and current value.
    auto itemRow = [&](MenuItem item, const char *&label, lib::StaticString<12> &value) {
        constexpr const char *Powers[] = {"Low", "Mid", "High", "Turbo"};
        const services::KeyLockSettings &k = keyLock.settings();
        switch (item) {
        case MenuItem::Squelch:
            label = "Squelch";
            if (channels->squelch() == 0) {
                value << "Open";
            } else {
                value << channels->squelch();
            }
            break;
        case MenuItem::SquelchMode:
            label = "Sq type";
            value << (channels->current() && channels->current()->squelchMode == SquelchMode::Tone ? "Tone" : "Carrier");
            break;
        case MenuItem::Power:
            label = "Power";
            value << (channels->current() ? Powers[static_cast<int>(channels->current()->power)] : "-");
            break;
        case MenuItem::Backlight:
            label = "Backlight";
            value << backlightLevel;
            break;
        case MenuItem::BacklightTime:
            label = "Light time";
            value << durationText(backlightS).view();
            break;
        case MenuItem::BatterySave:
            label = "Battery save";
            value << batterySaveText(batterySave);
            break;
        case MenuItem::KeyLock:
            label = "Lock covers";
            value << (k.knob ? "All" : k.keypad && k.side ? "Keys"
                    : k.keypad                            ? "Keypad"
                                                          : "Side");
            break;
        case MenuItem::AutoLock:
            label = "Auto lock";
            if (k.autoLockMs == 0) {
                value << "Off";
            } else {
                value << k.autoLockMs / 1000 << " s";
            }
            break;
        case MenuItem::Count:
            break;
        }
    };
    // The top of the menu: the groups.
    auto openMenu = [&] {
        menu.open("Menu");
        for (std::uint8_t g = 0; g < static_cast<std::uint8_t>(MenuGroup::Count); ++g) {
            menu.add(g, Groups[g].name, ">");
        }
        menu.select(static_cast<std::uint8_t>(menuGroup));
        menuAtGroups = true;
        menuQuick = false;
        view = View::Menu;
        redrawView();
    };
    // One group's settings, each with its current value.
    auto openGroup = [&](MenuGroup group, bool quick) {
        const GroupInfo &g = Groups[static_cast<int>(group)];
        menuGroup = group;
        menu.open(g.name);
        for (std::uint8_t i = 0; i < g.count; ++i) {
            const char *label = "";
            lib::StaticString<12> value;
            itemRow(g.items[i], label, value);
            menu.add(static_cast<std::uint8_t>(g.items[i]), label, value.view());
        }
        menu.select(static_cast<std::uint8_t>(menuItem));
        menuAtGroups = false;
        menuQuick = quick;
        view = View::Menu;
        redrawView();
    };
    auto openMenuValue = [&](MenuItem item) {
        menuItem = item;
        switch (item) {
        case MenuItem::Squelch:
            menuValue.open("Squelch");
            menuValue.add(0, "Open");
            for (std::uint8_t i = 1; i <= 5; ++i) {
                lib::StaticString<4> s;
                s << i;
                menuValue.add(i, s.view());
            }
            menuValue.select(channels->squelch());
            break;
        case MenuItem::SquelchMode:
            menuValue.open("Sq type");
            menuValue.add(0, "Carrier");
            menuValue.add(1, "Tone");
            if (channels->current()) {
                menuValue.select(channels->current()->squelchMode == SquelchMode::Tone ? 1 : 0);
            }
            break;
        case MenuItem::Power:
            menuValue.open("Power");
            menuValue.add(0, "Low");
            menuValue.add(1, "Mid");
            menuValue.add(2, "High");
            menuValue.add(3, "Turbo");
            if (channels->current()) {
                menuValue.select(static_cast<std::uint8_t>(channels->current()->power));
            }
            break;
        case MenuItem::Backlight:
            menuValue.open("Backlight");
            for (std::uint8_t i = 1; i <= Display::BacklightLevels; ++i) {
                lib::StaticString<4> s;
                s << i;
                menuValue.add(i, s.view());
            }
            menuValue.select(static_cast<std::uint8_t>(backlightLevel));
            break;
        case MenuItem::BacklightTime:
            menuValue.open("Light time");
            for (std::uint8_t i = 0; i < std::size(BacklightTimes); ++i) {
                menuValue.add(i, durationText(BacklightTimes[i]).view());
                if (BacklightTimes[i] == backlightS) {
                    menuValue.select(i);
                }
            }
            break;
        case MenuItem::BatterySave:
            menuValue.open("Battery save");
            menuValue.add(0, "Off");
            menuValue.add(1, "On", "150 ms sleep");
            menuValue.add(2, "Fast wake", "70 ms sleep");
            menuValue.select(batterySave);
            break;
        case MenuItem::KeyLock:
            menuValue.open("Lock covers");
            menuValue.add(0, "All", "keys, PTT, knob");
            menuValue.add(1, "Keys", "keys and PTT");
            menuValue.add(2, "Keypad", "front keys");
            menuValue.add(3, "Side", "side keys, PTT");
            break;
        case MenuItem::AutoLock:
            menuValue.open("Auto lock");
            menuValue.add(0, "Off");
            menuValue.add(10, "10 s");
            menuValue.add(30, "30 s");
            menuValue.add(60, "60 s");
            menuValue.select(static_cast<std::uint8_t>(keyLock.settings().autoLockMs / 1000));
            break;
        case MenuItem::Count:
            return;
        }
        view = View::MenuValue;
        redrawView();
    };
    auto useChannel = [&] {
        if (channels == nullptr || !channels->current()) {
            return;
        }
        radio::post(radio::UseChannel{*channels->current(), channels->squelch()});
        if (view == View::Standby) {
            redrawView();
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
    // The value chosen in the menu takes effect at once, for this session.
    auto applyMenuValue = [&] {
        const std::uint8_t v = menuValue.chosen();
        switch (menuItem) {
        case MenuItem::Squelch:
            channels->setSquelch(v);
            log::info("ui") << "squelch " << v;
            break;
        case MenuItem::SquelchMode:
            channels->setSquelchMode(v == 1 ? SquelchMode::Tone : SquelchMode::Carrier);
            log::info("ui") << "squelch opens on " << (v == 1 ? "the tone" : "the carrier");
            break;
        case MenuItem::Power:
            channels->setPower(static_cast<PowerLevel>(v));
            log::info("ui") << "power " << v;
            break;
        case MenuItem::Backlight:
            backlightLevel = v;
            d.setBacklight(v);
            break;
        case MenuItem::BacklightTime:
            backlightS = BacklightTimes[v < std::size(BacklightTimes) ? v : 0];
            log::info("ui") << "light time " << durationText(backlightS).view();
            break;
        case MenuItem::BatterySave:
            batterySave = v;
            radio::post(radio::BatterySave{v});
            break;
        case MenuItem::KeyLock: {
            services::KeyLockSettings s = keyLock.settings();
            s.keypad = v != 3;
            s.side = v != 2;
            s.knob = v == 0;
            keyLock.setSettings(s);
            logLockSettings();
            break;
        }
        case MenuItem::AutoLock: {
            services::KeyLockSettings s = keyLock.settings();
            s.autoLockMs = static_cast<std::uint32_t>(v) * 1000;
            keyLock.setSettings(s);
            logLockSettings();
            break;
        }
        case MenuItem::Count:
            break;
        }
        if (menuItem == MenuItem::Squelch || menuItem == MenuItem::SquelchMode || menuItem == MenuItem::Power) {
            useChannel();
        }
        // Back to the list it came from, showing the new value; straight to
        // the standby screen when opened from there.
        if (menuQuick) {
            show(View::Standby);
        } else {
            openGroup(menuGroup, false);
        }
    };
    // A key on the standby screen or the zone list, after the lock let it
    // through.
    auto channelKey = [&](Key key, KeyDebouncer::EventType type) {
        using Type = KeyDebouncer::EventType;
        const bool press = type == Type::Press;
        const bool step = press || type == Type::Repeat;
        if (view == View::Menu || view == View::MenuValue) {
            ListPicker &list = view == View::Menu ? menu : menuValue;
            if (step && (key == Key::Up || key == Key::Down)) {
                list.move(key == Key::Up ? -1 : 1);
                list.draw(d, StandbyScreen::StatusH);
            } else if (press && key == Key::Menu) {
                if (view == View::MenuValue) {
                    applyMenuValue();
                } else if (menuAtGroups) {
                    openGroup(static_cast<MenuGroup>(menu.chosen()), false);
                } else {
                    openMenuValue(static_cast<MenuItem>(menu.chosen()));
                }
            } else if (press && key == Key::Exit) {
                if (view == View::MenuValue) {
                    openGroup(menuGroup, menuQuick);
                } else if (!menuAtGroups && !menuQuick) {
                    openMenu();
                } else {
                    show(View::Standby);
                }
            }
            return;
        }
        if (view == View::ZonePicker) {
            if (step && (key == Key::Up || key == Key::Down)) {
                zonePicker.move(key == Key::Up ? -1 : 1);
                zonePicker.draw(d, StandbyScreen::StatusH);
            } else if (press && (key == Key::Menu || key == Key::Front1)) {
                channels->selectZone(zonePicker.chosen());
                show(View::Standby);
                useChannel();
            } else if (press && key == Key::Exit) {
                show(View::Standby);
            }
            return;
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
            openMenu();
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
            openGroup(MenuGroup::Channel, true);
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
                               log::info("ui") << "light time " << durationText(backlightS).view();
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
                               if (channels != nullptr && (view == View::Standby || view == View::ZonePicker || view == View::Menu || view == View::MenuValue)) {
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
                           [&](const KeyLockSettings &s) {
                               keyLock.setSettings(s.settings);
                               refreshLockMark();
                               logLockSettings();
                           },
                           [&](const KeyLockReport &) { logLockSettings(); },
                           [&](const EncoderClicks &c) {
                               useLight(d);
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
                                   if (view == View::ZonePicker || view == View::Menu || view == View::MenuValue) {
                                       ListPicker &list = view == View::ZonePicker ? zonePicker
                                           : view == View::Menu                    ? menu
                                                                                   : menuValue;
                                       list.move(moved);
                                       list.draw(d, StandbyScreen::StatusH);
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
                                   standby.drawStatus(d, standbyState());
                                   drawBattery(d);
                                   drawVolumeNotice(d, step);
                                   noticeShown = true;
                                   noticeUntil = os::nowMs() + NoticeMs;
                               }
                           },
                           [&](const Battery &b) {
                               batteryState = {b.millivolts, b.level, b.low};
                               if (view != View::Start && view != View::TestPattern && view != View::Message) {
                                   drawBattery(d);
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
            redrawView();
        }
        if (view == View::Start && static_cast<std::int32_t>(os::nowMs() - startUntil) >= 0) {
            show(channels != nullptr ? View::Standby : View::KeyTest);
            radio::post(radio::BatterySave{batterySave});
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

void start(Display &d, CodeplugSource *c, const board::Input *i)
{
    display = &d;
    codeplug = c;
    input = i;
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
