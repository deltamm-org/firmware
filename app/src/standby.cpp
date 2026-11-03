// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "standby.h"

#include "menu_screen.h"

#include "dmm/lib/format.h"
#include "dmm/services/font.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace dmm::app::ui {

namespace {

using namespace services;
namespace p = palette;

const Font &Small = fonts::Spleen5x8;
const Font &Body = fonts::Spleen6x12;
const Font &Medium = fonts::Spleen8x16;
const Font &Large = fonts::Spleen12x24;

// A filled tag such as "TX" in the status bar, from @p x; returns the x
// after it. The text goes first, with the tag's colour behind it, then the
// rest of the box: nothing is painted twice, so nothing flickers.
int tag(Display &d, int x, std::string_view text, Color fill, Color ink)
{
    const int w = textWidth(Small, text) + 4;
    const int rowH = StandbyScreen::StatusH - 1;
    drawText(d, Small, x + 2, 2, text, ink, fill);
    fillArea(d, x, 0, w, 1, p::Ground);
    fillArea(d, x, 1, w, 1, fill);
    fillArea(d, x, 2, 2, Small.height, fill);
    fillArea(d, x + w - 2, 2, 2, Small.height, fill);
    fillArea(d, x, 2 + Small.height, w, rowH - 2 - Small.height, fill);
    return x + w;
}

// Frequency as MHz with four decimals and no trailing zero past three.
lib::StaticString<12> mhz(std::uint32_t hz)
{
    lib::StaticString<12> s;
    const std::uint32_t tenthKhz = hz / 100;
    s << tenthKhz / 10000 << '.';
    const std::uint32_t frac = tenthKhz % 10000;
    s << static_cast<char>('0' + frac / 1000) << static_cast<char>('0' + frac / 100 % 10)
      << static_cast<char>('0' + frac / 10 % 10);
    if (frac % 10 != 0) {
        s << static_cast<char>('0' + frac % 10);
    }
    return s;
}

lib::StaticString<12> toneText(const Tone &t)
{
    lib::StaticString<12> s;
    if (t.kind == Tone::Kind::Ctcss) {
        s << t.value / 10 << '.' << static_cast<char>('0' + t.value % 10);
    } else if (t.kind == Tone::Kind::Dcs) {
        s << 'D' << static_cast<char>('0' + (t.value >> 6 & 7)) << static_cast<char>('0' + (t.value >> 3 & 7))
          << static_cast<char>('0' + (t.value & 7)) << (t.inverted ? 'I' : 'N');
    }
    return s;
}

const char *powerText(PowerLevel p)
{
    constexpr const char *Names[] = {"LOW", "MID", "HIGH", "TURBO"};
    return Names[static_cast<int>(p) & 3];
}

// The typed digits as a frequency with the rest of its places shown as
// underscores: "1453" -> "145.3__".
lib::StaticString<12> entryText(std::string_view digits)
{
    lib::StaticString<12> s;
    for (std::size_t i = 0; i < 6; ++i) {
        if (i == 3) {
            s << '.';
        }
        s << (i < digits.size() ? digits[i] : '_');
    }
    return s;
}

// Copies @p text into a fixed buffer, cut to fit.
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

void StandbyScreen::drawStatus(Display &d, const StandbyState &s)
{
    // The status row on black, a rule under it; the caller draws the
    // battery at its right end. Each part is drawn in place, with only the
    // gaps painted ground, so a redraw does not flicker.
    const int rowH = StatusH - 1;
    int cursor = 0;
    auto gapTo = [&](int x) {
        fillArea(d, cursor, 0, x - cursor, rowH, p::Ground);
        cursor = std::max(cursor, x);
    };
    gapTo(2);
    if (s.transmitting) {
        cursor = tag(d, cursor, "TX", p::Transmit, p::Text);
        gapTo(cursor + 2);
    } else if (s.receiving) {
        cursor = tag(d, cursor, "RX", p::Receive, p::Text);
        gapTo(cursor + 2);
    }
    if (s.locked) {
        cursor = tag(d, cursor, "LOCK", p::Warn, p::Ground);
    }
    // GPS: amber while asleep between fixes, green with a fix, grey while
    // searching, red without data.
    if (s.gps.on) {
        gapTo(cursor + 2);
        if (s.gps.asleep) {
            cursor = tag(d, cursor, "GPS", p::Warn, p::Ground);
        } else if (s.gps.fix.valid) {
            cursor = tag(d, cursor, "GPS", p::Receive, p::Text);
        } else if (s.gps.receiving) {
            cursor = tag(d, cursor, "GPS", p::Rule, p::Dim);
        } else {
            cursor = tag(d, cursor, "GPS", p::Transmit, p::Text);
        }
    }
    gapTo(batteryX(d));
    d.fillRect(0, StatusH - 1, d.width(), 1, p::Rule);
}

namespace {

using Layout = services::HomeLayout;

// The home screen's rows, top to bottom.
constexpr int LabelY = StandbyScreen::StatusH; // the top line and the clock
constexpr int NameY = LabelY + 19;             // the large name
constexpr int PanelY = NameY + 28;             // the middle row, on a panel
constexpr int SettingsY = PanelY + 22;         // the power and the reading beside it
constexpr int InfoY = SettingsY + 18;          // the bottom row
// Where the right part of the top line and of the power row starts.
constexpr int RightX = 96;
// The signal meter's bars.
constexpr int MeterX = 26;
constexpr int MeterStep = 7;

// The channel is a DMR channel with a contact, not tuned freely.
bool hasTalkgroup(const ChannelSelector &sel, const StandbyState &s)
{
    const auto &c = sel.current();
    return c && c->digital() && !s.talkgroup.empty() && s.entry.empty() && !sel.vfoMode();
}

// What the large name is.
enum class Big
{
    Entry,
    None, // no channel
    Zone,
    ContactName,
    Contact, // "TG 9": a contact without a name on a channel without one
    Frequency,
    Channel,
};

Big bigShows(const ChannelSelector &sel, const StandbyState &s)
{
    const auto &c = sel.current();
    if (!s.entry.empty()) {
        return Big::Entry;
    }
    if (!c) {
        return Big::None;
    }
    const bool talkgroup = hasTalkgroup(sel, s);
    const bool unnamed = c->name[0] == '\0';
    if (s.layout.big == Layout::Big::Zone && !sel.vfoMode() && sel.zoneName()[0] != '\0') {
        return Big::Zone;
    }
    if (talkgroup && (s.layout.big == Layout::Big::Contact || unnamed) && !s.talkgroupName.empty()) {
        return Big::ContactName;
    }
    if (talkgroup && unnamed) {
        return Big::Contact;
    }
    return sel.vfoMode() || unnamed ? Big::Frequency : Big::Channel;
}

// "Last: NAME  3 min ago" in at most @p chars characters, the name cut
// to make room for the age.
lib::StaticString<48> lastCallText(const StandbyState &s, int chars)
{
    lib::StaticString<48> t;
    if (s.readings.lastCallAt == 0) {
        t << "No call heard yet";
        return t;
    }
    lib::StaticString<16> age;
    const services::Age &a = s.readings.lastCallAge;
    switch (a.unit) {
    case services::Age::Unit::Now:
        age << "now";
        break;
    case services::Age::Unit::Minutes:
        age << a.value << " min ago";
        break;
    case services::Age::Unit::Hours:
        age << a.value << " h ago";
        break;
    }
    constexpr std::string_view Head = "Last: ";
    constexpr int Gap = 2;
    const int room = chars - static_cast<int>(Head.size() + age.view().size()) - Gap;
    t << Head << cut(s.lastCaller, room) << "  " << age.view();
    return t;
}

} // namespace

void StandbyScreen::drawClock(Display &d, const StandbyState &s)
{
    const std::string_view clock = s.layout.clock ? s.clock : std::string_view{};
    band(d, LabelY, NameY - LabelY, {{d.width() - 4 - textWidth(Medium, clock), LabelY + 2, clock, &Medium, p::Text}},
        p::Ground, RightX);
}

void StandbyScreen::drawMiddle(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    const auto &c = sel.current();
    const int w = d.width();
    const int hh = SettingsY - PanelY;
    const bool talkgroup = hasTalkgroup(sel, s);
    Layout::Middle middle = s.layout.middle;
    if (!s.entry.empty() || !c || (middle == Layout::Middle::Talkgroup && !talkgroup)) {
        middle = Layout::Middle::Frequency;
    }

    switch (middle) {
    case Layout::Middle::Frequency:
    case Layout::Middle::Count: {
        // Frequency, slot and colour code; on FM the bandwidth.
        lib::StaticString<24> freq;
        lib::StaticString<8> slot;
        lib::StaticString<16> slotNote;
        if (s.entry.empty() && c && s.roaming && s.roamSearching) {
            freq << "Searching";
        } else if (s.entry.empty() && c) {
            freq << mhz(s.roaming && s.roamHz != 0 ? s.roamHz : c->receiveHz).view();
            if (c->digital()) {
                slot << "TS" << c->dmr.slot;
                slotNote << "CC" << (s.roaming ? s.roamColourCode : c->dmr.colourCode);
            } else {
                slot << "FM";
                slotNote << (c->wide ? "25k" : "12.5k");
            }
        }
        const bool unit = !slot.view().empty();
        band(d, PanelY, hh,
            {{4, PanelY + 7, unit ? "MHz" : "", &Small, p::Dim}, {unit ? 26 : 4, PanelY + 5, freq.view(), &Body, p::Text},
                {RightX, PanelY + 5, slot.view(), &Body, p::Text},
                {RightX + 24, PanelY + 8, slotNote.view(), &Small, p::Dim}},
            p::Panel);
        return;
    }
    case Layout::Middle::Talkgroup: {
        // The contact, with its name small after it; the channel's name
        // instead when the contact's name is already large.
        const std::string_view name = bigShows(sel, s) == Big::ContactName ? std::string_view(c->name) : s.talkgroupName;
        const int nameX = 8 + textWidth(Body, s.talkgroup);
        band(d, PanelY, hh,
            {{4, PanelY + 5, s.talkgroup, &Body, p::Text},
                {nameX, PanelY + 8, cut(name, (w - 4 - nameX) / Small.width), &Small, p::Dim}},
            p::Panel);
        return;
    }
    case Layout::Middle::Signal:
        break;
    }

    // The signal meter, each pixel painted once: label, bars, then the
    // level in dBm.
    const services::HomeReadings &r = s.readings;
    band(d, PanelY, hh, {{4, PanelY + 7, "SIG", &Small, p::Dim}}, p::Panel, 0, MeterX);
    const int barY = PanelY + 6;
    const int barH = hh - 12;
    const int lit = r.signalKnown ? services::signalBars(r.signalDbm) : 0;
    const int barsEnd = MeterX + services::SignalBars * MeterStep;
    fillArea(d, MeterX, PanelY, RightX - MeterX, barY - PanelY, p::Panel);
    for (int i = 0; i < services::SignalBars; ++i) {
        const int x = MeterX + i * MeterStep;
        d.fillRect(x, barY, MeterStep - 1, barH, i < lit ? p::Text : p::Rule);
        d.fillRect(x + MeterStep - 1, barY, 1, barH, p::Panel);
    }
    fillArea(d, barsEnd, barY, RightX - barsEnd, barH, p::Panel);
    fillArea(d, MeterX, barY + barH, RightX - MeterX, PanelY + hh - barY - barH, p::Panel);
    lib::StaticString<8> level;
    if (r.signalKnown) {
        level << r.signalDbm;
    } else {
        level << '-';
    }
    band(d, PanelY, hh,
        {{RightX + 4, PanelY + 5, level.view(), &Body, p::Text}, {RightX + 36, PanelY + 8, "dBm", &Small, p::Dim}},
        p::Panel, RightX);
}

void StandbyScreen::drawBeside(Display &d, const StandbyState &s)
{
    const services::HomeReadings &r = s.readings;
    lib::StaticString<8> v;
    std::string_view label = "BAT";
    Color ink = p::Text;
    switch (s.layout.beside) {
    case Layout::Beside::Volume:
        label = "VOL";
        if (r.volume >= 0) {
            v << r.volume;
        }
        ink = r.volume == 0 ? p::Warn : p::Text;
        break;
    case Layout::Beside::Signal:
        label = "SIG";
        if (r.signalKnown) {
            v << r.signalDbm;
        } else {
            v << '-';
        }
        break;
    case Layout::Beside::Voltage:
    case Layout::Beside::Count:
        if (r.centivolts > 0) {
            v << r.centivolts / 100 << '.' << lib::zeroPadded(static_cast<std::uint32_t>(r.centivolts % 100), 2) << 'V';
            ink = r.batteryLow ? p::Transmit : p::Text;
        }
        break;
    }
    band(d, SettingsY, InfoY - SettingsY - 1,
        {{RightX, SettingsY + 5, label, &Small, p::Dim}, {RightX + 22, SettingsY + 3, v.view(), &Body, ink}}, p::Ground,
        RightX);
}

lib::StaticString<48> bottomText(const ChannelSelector &sel, const StandbyState &s, int chars)
{
    const auto &c = sel.current();
    lib::StaticString<48> info;
    if (!s.entry.empty()) {
        info << "Menu: tune  Exit: cancel";
    } else if (s.layout.bottom == Layout::Bottom::LastCaller) {
        info << lastCallText(s, chars).view();
    } else if (s.layout.bottom == Layout::Bottom::Position) {
        if (s.gps.on && s.gps.fix.valid) {
            info << latitudeText(s.gps.fix.latitudeE7).view() << "  " << longitudeText(s.gps.fix.longitudeE7).view();
        } else {
            info << "No GPS position";
        }
    } else if (s.layout.bottom == Layout::Bottom::None) {
    } else if (c && s.roaming) {
        // The repeater a roaming channel works through, counted from 1.
        if (s.roamSearching) {
            info << "Looking for a repeater";
        } else if (s.roamPosition > 0) {
            info << "via " << (s.roamSite.empty() ? "repeater " : "") << s.roamPosition;
            if (!s.roamSite.empty()) {
                info << ' ' << s.roamSite;
            }
        } else {
            info << "via the channel's repeater";
        }
    } else if (c) {
        if (c->analog()) {
            // Tones and offset on FM.
            if (c->transmitTone.kind != Tone::Kind::None) {
                info << 'T' << toneText(c->transmitTone).view() << "  ";
            }
            if (c->receiveTone.kind != Tone::Kind::None) {
                info << 'R' << toneText(c->receiveTone).view() << "  ";
            }
            if (c->transmitHz != c->receiveHz) {
                const bool up = c->transmitHz > c->receiveHz;
                const std::uint32_t off = up ? c->transmitHz - c->receiveHz : c->receiveHz - c->transmitHz;
                info << (up ? '+' : '-') << mhz(off).view();
            }
        }
        if (info.size() == 0 && !sel.vfoMode()) {
            // The channel's name when something else is large; else the zone.
            const bool channelLarge = bigShows(sel, s) == Big::Channel;
            info << cut(channelLarge ? sel.zoneName() : std::string_view(c->name), 16) << "  " << sel.position() << '/'
                 << sel.zoneSize();
        }
    }
    if (c && c->receiveOnly && s.entry.empty()) {
        info << "  RX ONLY";
    }
    lib::StaticString<48> text;
    text << cut(info.view(), chars);
    return text;
}

void StandbyScreen::drawBottom(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    const auto text = bottomText(sel, s, (d.width() - 8) / Small.width);
    band(d, InfoY, d.height() - HintH - InfoY, {{4, InfoY + 5, text.view(), &Small, p::Dim}});
}

void StandbyScreen::draw(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    const int w = d.width();
    const int h = d.height();
    drawStatus(d, s);
    const auto &c = sel.current();

    // The top line, left of the clock.
    lib::StaticString<24> top;
    if (!s.entry.empty()) {
        top << "ENTER MHz";
    } else if (sel.vfoMode()) {
        top << "VFO";
    } else {
        switch (s.layout.top) {
        case Layout::Top::Talkgroup:
            top << (hasTalkgroup(sel, s) ? s.talkgroup : std::string_view("CHANNEL"));
            break;
        case Layout::Top::Zone:
            top << sel.zoneName();
            break;
        case Layout::Top::Number:
            if (c) {
                top << "CH " << sel.position() << '/' << sel.zoneSize();
            }
            break;
        case Layout::Top::None:
        case Layout::Top::Count:
            break;
        }
    }
    band(d, LabelY, NameY - LabelY, {{4, LabelY + 4, cut(top.view(), (RightX - 8) / Small.width), &Small, p::Dim}},
        p::Ground, 0, RightX);
    drawClock(d, s);

    // The large name; a long one takes the smaller font.
    std::string_view primary;
    lib::StaticString<12> f;
    switch (bigShows(sel, s)) {
    case Big::Entry:
        f = entryText(s.entry);
        primary = f.view();
        break;
    case Big::None:
        primary = "No channel";
        break;
    case Big::Zone:
        primary = sel.zoneName();
        break;
    case Big::ContactName:
        primary = s.talkgroupName;
        break;
    case Big::Contact:
        primary = s.talkgroup;
        break;
    case Big::Frequency:
        f = mhz(c->receiveHz);
        primary = f.view();
        break;
    case Big::Channel:
        primary = c->name;
        break;
    }
    const Font &pf = textWidth(Large, primary) <= w - 8 ? Large : Medium;
    const int py = NameY + (Large.height - pf.height) / 2 + 1;
    band(d, NameY, PanelY - NameY - 1, {{4, py, cut(primary, (w - 8) / pf.width), &pf, p::Text}});
    d.fillRect(0, PanelY - 1, w, 1, p::Rule);

    drawMiddle(d, sel, s);

    // The power, and the reading beside it.
    band(d, SettingsY, InfoY - SettingsY - 1,
        {{4, SettingsY + 5, c ? "PWR" : "", &Small, p::Dim},
            {26, SettingsY + 3, c ? std::string_view(powerText(c->power)) : std::string_view{}, &Body, p::Text}},
        p::Ground, 0, RightX);
    drawBeside(d, s);
    d.fillRect(0, InfoY - 1, w, 1, p::Rule);

    drawBottom(d, sel, s);

    // Bottom bar: the quick keys, or a short note.
    d.fillRect(0, h - HintH, w, 1, p::Rule);
    const std::string_view hints = s.note.empty() ? s.keys : s.note;
    band(d, h - HintH + 1, HintH - 1, {{3, h - HintH + 2, hints, &Small, p::Dim}});
    m_shown = s.readings;
}

std::uint8_t StandbyScreen::refresh(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    namespace rows = services::home_rows;
    const std::uint8_t changed = services::changedRows(s.layout, m_shown, s.readings);
    if ((changed & rows::Clock) != 0) {
        drawClock(d, s);
    }
    if ((changed & rows::Middle) != 0) {
        drawMiddle(d, sel, s);
    }
    if ((changed & rows::Beside) != 0) {
        drawBeside(d, s);
    }
    if ((changed & rows::Bottom) != 0) {
        drawBottom(d, sel, s);
    }
    m_shown = s.readings;
    return changed;
}

void StandbyScreen::drawCall(Display &d, const StandbyState &s, const CallInfo &c)
{
    const int w = d.width();
    const int h = d.height();
    drawStatus(d, s);
    const Color ink = c.active ? p::Text : p::Dim;

    // The call type, green while it is heard, and the slot.
    const int typeY = StatusH + 3;
    const int callerY = typeY + Body.height + 4;
    const char *type = c.allCall ? "ALL CALL" : c.group ? "GROUP CALL"
                                                        : "PRIVATE CALL";
    lib::StaticString<24> head;
    head << (c.active ? "" : "LAST ") << type;
    lib::StaticString<4> ts;
    if (c.slot != 0) {
        ts << "TS" << c.slot;
    }
    band(d, StatusH, callerY - StatusH,
        {{4, typeY, head.view(), &Body, c.active ? p::Receive : p::Dim},
            {w - 4 - textWidth(Body, ts.view()), typeY, ts.view(), &Body, p::Dim}});

    // The caller: callsign or name large, or the ID when neither is
    // known; under it the user's name and the ID.
    const int callerIdY = callerY + Large.height + 1;
    lib::StaticString<12> callerId;
    callerId << c.callerId;
    const bool known = !c.callerHeadline.empty();
    const std::string_view caller = known ? c.callerHeadline : callerId.view();
    const Font &cf = textWidth(Large, caller) <= w - 8 ? Large : Medium;
    band(d, callerY, callerIdY - callerY, {{4, callerY + (Large.height - cf.height) / 2, caller, &cf, ink}});
    const int ruleY = callerIdY + Body.height + 3;
    if (known) {
        const int idX = w - 4 - textWidth(Body, callerId.view());
        const std::string_view name = cut(c.callerName, (idX - 10) / Body.width);
        band(d, callerIdY, ruleY - callerIdY,
            {{4, callerIdY, name, &Body, ink}, {idX, callerIdY, callerId.view(), &Body, p::Dim}});
    } else {
        band(d, callerIdY, ruleY - callerIdY, {});
    }

    // Whom: the talkgroup's or contact's name, its number under it.
    fillArea(d, 0, ruleY, 4, 1, p::Ground);
    d.fillRect(4, ruleY, w - 8, 1, p::Rule);
    fillArea(d, w - 4, ruleY, 4, 1, p::Ground);
    const int targetY = ruleY + 4;
    const int numberY = targetY + Medium.height + 1;
    lib::StaticString<16> number;
    if (c.allCall) {
        number << "All";
    } else {
        number << (c.group ? "TG " : "To ") << c.targetId;
    }
    const bool named = !c.targetName.empty() && !c.allCall;
    band(d, ruleY + 1, numberY - ruleY - 1, {{4, targetY, named ? c.targetName : number.view(), &Medium, ink}});
    band(d, numberY, h - HintH - numberY, {{4, numberY, named ? number.view() : std::string_view{}, &Body, p::Dim}});
    drawCallTime(d, c);
}

void StandbyScreen::drawCallTime(Display &d, const CallInfo &c)
{
    // The bottom bar: the call's time, the channel on the right.
    const int w = d.width();
    const int h = d.height();
    d.fillRect(0, h - HintH, w, 1, p::Rule);
    lib::StaticString<12> t;
    t << c.seconds / 60 << ':' << static_cast<char>('0' + c.seconds % 60 / 10) << static_cast<char>('0' + c.seconds % 10);
    const int room = w - 6 - textWidth(Small, t.view()) - 8;
    const std::string_view channel = cut(c.channel, room / Small.width);
    band(d, h - HintH + 1, HintH - 1,
        {{3, h - HintH + 2, t.view(), &Small, c.active ? p::Text : p::Dim},
            {w - 3 - textWidth(Small, channel), h - HintH + 2, channel, &Small, p::Dim}});
}

void drawGpsScreen(Display &d, const GpsState &gps, bool full)
{
    const int w = d.width();
    const int h = d.height();
    const int top = StandbyScreen::StatusH;
    constexpr const char *Labels[] = {"Lat", "Lon", "Alt", "Sats", "Speed", "UTC"};
    const int firstRow = top + Medium.height + 5;
    if (full) {
        d.fillRect(0, top, w, h - top, p::Ground);
        drawText(d, Medium, 4, top + 2, "GPS", p::Text, p::Ground);
        for (int i = 0; i < static_cast<int>(std::size(Labels)); ++i) {
            drawText(d, Body, 4, firstRow + i * (Body.height + 1), Labels[i], p::Dim, p::Ground);
        }
        d.fillRect(0, h - StandbyScreen::HintH, w, 1, p::Rule);
    }

    const services::GpsFix &f = gps.fix;
    std::string_view state = "Off";
    Color stateInk = p::Dim;
    if (gps.on && gps.asleep) {
        state = "Sleeping";
        stateInk = p::Warn;
    } else if (gps.on && f.valid) {
        state = "Fix";
        stateInk = p::Receive;
    } else if (gps.on && gps.receiving) {
        state = "Searching";
    } else if (gps.on) {
        state = "No data";
        stateInk = p::Transmit;
    }
    // Right-aligned in a field as wide as the longest state, so the spaces
    // cover a longer word drawn before.
    constexpr std::size_t StateChars = 9;
    lib::StaticString<StateChars + 1> padded;
    for (std::size_t i = state.size(); i < StateChars; ++i) {
        padded << ' ';
    }
    padded << state;
    drawText(d, Medium, w - 4 - textWidth(Medium, padded.view()), top + 2, padded.view(), stateInk, p::Ground);

    int y = firstRow;
    auto value = [&](std::string_view text) {
        line(d, Body, 46, y, w - 50, text, p::Text);
        y += Body.height + 1;
    };
    const bool position = gps.on && f.valid;
    value(position ? latitudeText(f.latitudeE7).view() : "-");
    value(position ? longitudeText(f.longitudeE7).view() : "-");
    lib::StaticString<24> v;
    if (position) {
        v << f.altitudeDm / 10 << " m";
    } else {
        v << "-";
    }
    value(v.view());
    v.clear();
    if (gps.on) {
        v << f.satellitesUsed << " of " << f.satellitesInView;
    } else {
        v << "-";
    }
    value(v.view());
    v.clear();
    if (position) {
        v << f.speedDeciKmh / 10 << '.' << f.speedDeciKmh % 10 << " km/h " << f.courseDeciDeg / 10 << " deg";
    } else {
        v << "-";
    }
    value(v.view());
    v.clear();
    if (gps.on && f.timeValid) {
        v << lib::zeroPadded(f.hour, 2) << ':' << lib::zeroPadded(f.minute, 2) << ':' << lib::zeroPadded(f.second, 2);
        if (f.dateValid) {
            v << ' ' << f.year << '-' << lib::zeroPadded(f.month, 2) << '-' << lib::zeroPadded(f.day, 2);
        }
    } else {
        v << "-";
    }
    value(v.view());

    line(d, Small, 3, h - StandbyScreen::HintH + 2, w - 6, "Exit: back", p::Dim);
}

void ListPicker::open(std::string_view title)
{
    m_count = 0;
    m_cursor = 0;
    copyText(m_title, title);
}

void ListPicker::add(std::uint8_t id, std::string_view text, std::string_view value)
{
    if (m_count >= MaxItems) {
        return;
    }
    m_ids[m_count] = id;
    copyText(m_text[m_count], text);
    copyText(m_value[m_count], value);
    ++m_count;
}

void ListPicker::select(std::uint8_t id)
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_ids[i] == id) {
            m_cursor = i;
        }
    }
}

void ListPicker::move(int steps)
{
    if (m_count == 0) {
        return;
    }
    const int n = static_cast<int>(m_count);
    m_cursor = static_cast<std::size_t>(((static_cast<int>(m_cursor) + steps) % n + n) % n);
}

void ListPicker::draw(Display &d, std::string_view hint)
{
    ListRow rows[ListRows];
    const std::size_t first = firstShown(m_cursor, m_count, ListRows);
    std::size_t n = 0;
    for (; n < std::size(rows) && first + n < m_count; ++n) {
        rows[n] = {m_text[first + n], m_value[first + n]};
    }
    drawListScreen(d, m_title, std::span(rows, n), first, m_count, m_cursor, hint);
}

} // namespace dmm::app::ui
