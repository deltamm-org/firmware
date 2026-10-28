// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "standby.h"

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

int textWidth(const Font &f, std::string_view s)
{
    return static_cast<int>(s.size()) * f.width;
}

// At most @p chars characters of @p s.
std::string_view cut(std::string_view s, int chars)
{
    return {s.data(), chars <= 0 ? 0 : std::min(s.size(), static_cast<std::size_t>(chars))};
}

// A filled rectangle; nothing when it is empty.
void fillArea(Display &d, int x, int y, int w, int h, Color c)
{
    if (w > 0 && h > 0) {
        d.fillRect(x, y, w, h, c);
    }
}

// One text of a band: where it goes, in which font and colour.
struct Piece
{
    int x;
    int y;
    std::string_view text;
    const Font *font;
    Color ink;
};

// A full-width band of the screen, [y, y + h): each piece of text is drawn
// with its own background first, then every other pixel of the band is
// painted ground. Nothing is cleared before it is drawn over, so a redraw
// does not flicker, and text of any size fits as long as it lies inside
// the band. The pieces must not overlap side by side.
void band(Display &d, int y, int h, std::initializer_list<Piece> pieces)
{
    const int w = d.width();
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
    int cursor = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Piece &pc = sorted[i];
        const int x = std::max(pc.x, cursor);
        const int end = std::min(drawText(d, *pc.font, x, pc.y, pc.text, pc.ink, p::Ground), w);
        fillArea(d, cursor, y, x - cursor, h, p::Ground);
        fillArea(d, x, y, end - x, pc.y - y, p::Ground);
        fillArea(d, x, pc.y + pc.font->height, end - x, y + h - pc.y - pc.font->height, p::Ground);
        cursor = end;
    }
    fillArea(d, cursor, y, w - cursor, h, p::Ground);
}

void line(Display &d, const Font &f, int x, int y, int w, std::string_view text, Color ink)
{
    const int end = drawText(d, f, x, y, text, ink, p::Ground);
    if (end < x + w) {
        d.fillRect(end, y, x + w - end, f.height, p::Ground);
    }
}

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

// The status bar's volume field: a fixed width, right-aligned against the
// battery, so a new step overwrites the old in place.
constexpr int VolumeChars = 6; // "VOL 15"
int volumeX(const Display &d, const StandbyScreen &s)
{
    return s.batteryX(d) - VolumeChars * Small.width - 4;
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
    // The local time, left of the volume.
    if (!s.clock.empty()) {
        const int w = textWidth(Small, s.clock);
        const int x = volumeX(d, *this) - w - 6;
        gapTo(x);
        drawText(d, Small, x, 2, s.clock, p::Text, p::Ground);
        fillArea(d, x, 0, w, 2, p::Ground);
        fillArea(d, x, 2 + Small.height, w, rowH - 2 - Small.height, p::Ground);
        cursor = x + w;
    }
    gapTo(volumeX(d, *this));
    drawVolume(d, s);
    cursor = volumeX(d, *this) + VolumeChars * Small.width;
    gapTo(batteryX(d));
    d.fillRect(0, StatusH - 1, d.width(), 1, p::Rule);
}

void StandbyScreen::drawVolume(Display &d, const StandbyState &s)
{
    // Right-aligned in its fixed field, blank when the step is not known.
    lib::StaticString<VolumeChars + 1> v;
    if (s.volume >= 0) {
        v << "VOL " << s.volume;
    }
    lib::StaticString<VolumeChars + 1> field;
    for (std::size_t i = v.view().size(); i < VolumeChars; ++i) {
        field << ' ';
    }
    field << v.view();
    const int x = volumeX(d, *this);
    const int w = VolumeChars * Small.width;
    drawText(d, Small, x, 2, field.view(), s.volume == 0 ? p::Warn : p::Dim, p::Ground);
    fillArea(d, x, 0, w, 2, p::Ground);
    fillArea(d, x, 2 + Small.height, w, StatusH - 1 - 2 - Small.height, p::Ground);
    fillArea(d, x + w, 0, batteryX(d) - x - w, StatusH - 1, p::Ground);
}

void StandbyScreen::draw(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    const int w = d.width();
    const int h = d.height();
    drawStatus(d, s);

    // The body in bands, top to bottom, each drawn in place.
    const int zoneY = StatusH + 3;
    const int nameY = zoneY + Body.height + 4;
    const int freqY = nameY + Large.height + 2;
    const int detY = freqY + Medium.height + 6;
    const int det2Y = detY + Body.height + 1;
    const int bodyEnd = h - HintH;

    // Zone line: zone name, or VFO, and the position.
    lib::StaticString<24> right;
    if (sel.vfoMode()) {
        band(d, StatusH, nameY - StatusH, {{4, zoneY, "VFO", &Body, p::Dim}});
    } else {
        right << sel.position() << '/' << sel.zoneSize();
        band(d, StatusH, nameY - StatusH,
            {{4, zoneY, sel.zoneName(), &Body, p::Dim},
                {w - 4 - textWidth(Body, right.view()), zoneY, right.view(), &Body, p::Dim}});
    }

    const auto &c = sel.current();
    if (!s.entry.empty()) {
        const auto e = entryText(s.entry);
        band(d, nameY, freqY - nameY, {{(w - textWidth(Large, e.view())) / 2, nameY, e.view(), &Large, p::Text}});
        band(d, freqY, detY - freqY, {{4, freqY + 4, "MHz  Menu: tune  Exit: cancel", &Body, p::Dim}});
        band(d, detY, bodyEnd - detY, {});
    } else if (!c) {
        band(d, nameY, freqY - nameY, {{4, nameY, "No channel", &Medium, p::Dim}});
        band(d, freqY, bodyEnd - freqY, {});
    } else {
        const auto f = mhz(c->receiveHz);
        lib::StaticString<16> fl;
        if (sel.vfoMode() || c->name[0] == '\0') {
            band(d, nameY, freqY - nameY, {{(w - textWidth(Large, f.view())) / 2, nameY, f.view(), &Large, p::Text}});
        } else {
            // A long name takes the smaller font, centred in the same band.
            const std::string_view name(c->name);
            const Font &nf = textWidth(Large, name) <= w - 8 ? Large : Medium;
            band(d, nameY, freqY - nameY,
                {{(w - textWidth(nf, name)) / 2, nameY + (Large.height - nf.height) / 2, name, &nf, p::Text}});
            fl << f.view() << " MHz";
        }
        if (s.roaming) {
            // The repeater in use changes as the radio roams: small, under
            // the name.
            lib::StaticString<40> rl;
            if (s.roamSearching) {
                rl << "Searching...";
            } else {
                rl << mhz(s.roamHz != 0 ? s.roamHz : c->receiveHz).view() << "  TS" << c->dmr.slot << "  CC"
                   << s.roamColourCode;
                if (!s.roamSite.empty()) {
                    rl << "  " << s.roamSite;
                }
            }
            band(d, freqY, detY - freqY,
                {{(w - textWidth(Body, rl.view())) / 2, freqY + (Medium.height - Body.height) / 2, rl.view(), &Body,
                    p::Dim}});
        } else {
            band(d, freqY, detY - freqY, {{(w - textWidth(Medium, fl.view())) / 2, freqY, fl.view(), &Medium, p::Text}});
        }

        // Details: mode and width, tones, power, offset.
        lib::StaticString<40> det;
        if (s.roaming) {
            det << "DMR ROAMING";
        } else if (!c->analog()) {
            det << "DMR CC" << c->dmr.colourCode << " TS" << c->dmr.slot;
        } else {
            det << "FM " << (c->wide ? "25k" : "12.5k");
            if (c->transmitTone.kind != Tone::Kind::None) {
                det << "  T" << toneText(c->transmitTone).view();
            }
            if (c->receiveTone.kind != Tone::Kind::None) {
                det << "  R" << toneText(c->receiveTone).view();
            }
        }
        band(d, detY, det2Y - detY, {{4, detY, det.view(), &Body, p::Dim}});
        lib::StaticString<40> det2;
        det2 << powerText(c->power);
        if (c->transmitHz != c->receiveHz && !s.roaming) {
            const bool up = c->transmitHz > c->receiveHz;
            const std::uint32_t off = up ? c->transmitHz - c->receiveHz : c->receiveHz - c->transmitHz;
            det2 << "  " << (up ? '+' : '-') << mhz(off).view();
        }
        if (c->receiveOnly) {
            det2 << "  RX ONLY";
        }
        band(d, det2Y, bodyEnd - det2Y, {{4, det2Y, det2.view(), &Body, p::Dim}});
    }

    // Bottom bar: the quick keys, or a short note.
    d.fillRect(0, h - HintH, w, 1, p::Rule);
    const std::string_view hints = s.note.empty() ? s.keys : s.note;
    band(d, h - HintH + 1, HintH - 1, {{3, h - HintH + 2, hints, &Small, p::Dim}});
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

void ListPicker::draw(Display &d, int top)
{
    const int w = d.width();
    const int rowH = Body.height + 2;
    d.fillRect(0, top, w, rowH - 1, p::Ground);
    drawText(d, Body, 4, top + 1, m_title, p::Dim, p::Ground);
    d.fillRect(0, top + rowH - 1, w, 1, p::Rule);
    const int first = m_cursor < static_cast<std::size_t>(Rows) ? 0 : static_cast<int>(m_cursor) - Rows + 1;
    for (int r = 0; r < Rows; ++r) {
        const int y = top + rowH * (r + 1);
        const int i = first + r;
        if (i >= static_cast<int>(m_count)) {
            d.fillRect(0, y, w, rowH, p::Ground);
            continue;
        }
        const bool on = static_cast<std::size_t>(i) == m_cursor;
        const Color fill = on ? p::Text : p::Ground;
        d.fillRect(0, y, w, rowH, fill);
        drawText(d, Body, 6, y + 1, m_text[i], on ? p::Ground : p::Text, fill);
        const std::string_view value(m_value[i]);
        if (!value.empty()) {
            drawText(d, Body, w - 6 - textWidth(Body, value), y + 1, value, on ? p::Ground : p::Dim, fill);
        }
    }
    const int end = top + rowH * (Rows + 1);
    if (end < d.height()) {
        d.fillRect(0, end, w, d.height() - end, p::Ground);
    }
}

} // namespace dmm::app::ui
