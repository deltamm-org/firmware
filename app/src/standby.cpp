// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "standby.h"

#include "dmm/lib/format.h"
#include "dmm/services/font.h"

#include <algorithm>

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

// A filled tag such as "TX" in the status bar; returns the x after it.
int tag(Display &d, int x, std::string_view text, Color fill, Color ink)
{
    const int w = textWidth(Small, text) + 4;
    d.fillRect(x, 1, w, StandbyScreen::StatusH - 2, fill);
    drawText(d, Small, x + 2, 2, text, ink, fill);
    return x + w + 2;
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

void line(Display &d, const Font &f, int x, int y, int w, std::string_view text, Color ink)
{
    const int end = drawText(d, f, x, y, text, ink, p::Ground);
    if (end < x + w) {
        d.fillRect(end, y, x + w - end, f.height, p::Ground);
    }
}

} // namespace

void StandbyScreen::drawStatus(Display &d, const StandbyState &s)
{
    // The status row on black, a rule under it; the caller draws the
    // battery at its right end.
    d.fillRect(0, 0, d.width(), StatusH - 1, p::Ground);
    d.fillRect(0, StatusH - 1, d.width(), 1, p::Rule);
    int x = 2;
    if (s.transmitting) {
        x = tag(d, x, "TX", p::Transmit, p::Text);
    } else if (s.receiving) {
        x = tag(d, x, "RX", p::Receive, p::Text);
    }
    if (s.locked) {
        tag(d, x, "LOCK", p::Warn, p::Ground);
    }
    // Volume, left of the battery: a speaker mark and the step.
    if (s.volume >= 0) {
        lib::StaticString<8> v;
        v << "VOL " << s.volume;
        const int vx = batteryX(d) - textWidth(Small, v.view()) - 4;
        drawText(d, Small, vx, 2, v.view(), s.volume == 0 ? p::Warn : p::Dim, p::Ground);
    }
}

void StandbyScreen::draw(Display &d, const ChannelSelector &sel, const StandbyState &s)
{
    const int w = d.width();
    const int h = d.height();
    drawStatus(d, s);
    d.fillRect(0, StatusH, w, h - StatusH - HintH, p::Ground);

    // Zone line: zone name, or VFO, and the position.
    const int zoneY = StatusH + 3;
    lib::StaticString<24> right;
    if (sel.vfoMode()) {
        drawText(d, Body, 4, zoneY, "VFO", p::Dim, p::Ground);
    } else {
        drawText(d, Body, 4, zoneY, sel.zoneName(), p::Dim, p::Ground);
        right << sel.position() << '/' << sel.zoneSize();
        drawText(d, Body, w - 4 - textWidth(Body, right.view()), zoneY, right.view(), p::Dim, p::Ground);
    }

    const auto &c = sel.current();
    const int nameY = zoneY + Body.height + 4;
    const int freqY = nameY + Large.height + 2;
    if (!s.entry.empty()) {
        const auto e = entryText(s.entry);
        drawText(d, Large, (w - textWidth(Large, e.view())) / 2, nameY, e.view(), p::Text, p::Ground);
        drawText(d, Body, 4, freqY + 4, "MHz  Menu: tune  Exit: cancel", p::Dim, p::Ground);
    } else if (!c) {
        drawText(d, Medium, 4, nameY, "No channel", p::Dim, p::Ground);
    } else {
        const auto f = mhz(c->receiveHz);
        if (sel.vfoMode() || c->name[0] == '\0') {
            drawText(d, Large, (w - textWidth(Large, f.view())) / 2, nameY, f.view(), p::Text, p::Ground);
        } else {
            const std::string_view name(c->name);
            const Font &nf = textWidth(Large, name) <= w - 8 ? Large : Medium;
            drawText(d, nf, (w - textWidth(nf, name)) / 2, nameY + (Large.height - nf.height) / 2, name, p::Text,
                p::Ground);
            lib::StaticString<16> fl;
            fl << f.view() << " MHz";
            drawText(d, Medium, (w - textWidth(Medium, fl.view())) / 2, freqY, fl.view(), p::Text, p::Ground);
        }

        // Details: mode and width, tones, power, offset.
        lib::StaticString<40> det;
        if (!c->analog()) {
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
        const int detY = freqY + Medium.height + 6;
        {
            line(d, Body, 4, detY, w - 8, det.view(), p::Dim);
            lib::StaticString<40> det2;
            det2 << powerText(c->power);
            if (c->transmitHz != c->receiveHz) {
                const bool up = c->transmitHz > c->receiveHz;
                const std::uint32_t off = up ? c->transmitHz - c->receiveHz : c->receiveHz - c->transmitHz;
                det2 << "  " << (up ? '+' : '-') << mhz(off).view();
            }
            if (c->receiveOnly) {
                det2 << "  RX ONLY";
            }
            line(d, Body, 4, detY + Body.height + 1, w - 8, det2.view(), p::Dim);
        }
    }

    // Bottom bar: the quick keys, or a short note.
    d.fillRect(0, h - HintH, w, 1, p::Rule);
    d.fillRect(0, h - HintH + 1, w, HintH - 1, p::Ground);
    const std::string_view hints = s.note.empty() ? s.keys : s.note;
    drawText(d, Small, 3, h - HintH + 2, hints, p::Dim, p::Ground);
}

void StandbyScreen::drawCall(Display &d, const StandbyState &s, const CallInfo &c)
{
    const int w = d.width();
    const int h = d.height();
    drawStatus(d, s);
    d.fillRect(0, StatusH, w, h - StatusH - HintH, p::Ground);
    const Color ink = c.active ? p::Text : p::Dim;

    // The call type, green while it is heard, and the slot.
    const int typeY = StatusH + 3;
    const char *type = c.allCall ? "ALL CALL" : c.group ? "GROUP CALL"
                                                        : "PRIVATE CALL";
    lib::StaticString<24> head;
    head << (c.active ? "" : "LAST ") << type;
    drawText(d, Body, 4, typeY, head.view(), c.active ? p::Receive : p::Dim, p::Ground);
    if (c.slot != 0) {
        lib::StaticString<4> ts;
        ts << "TS" << c.slot;
        drawText(d, Body, w - 4 - textWidth(Body, ts.view()), typeY, ts.view(), p::Dim, p::Ground);
    }

    // The caller: callsign or name large, or the ID when neither is
    // known; under it the user's name and the ID.
    const int callerY = typeY + Body.height + 4;
    lib::StaticString<12> callerId;
    callerId << c.callerId;
    const bool known = !c.callerHeadline.empty();
    const std::string_view caller = known ? c.callerHeadline : callerId.view();
    const Font &cf = textWidth(Large, caller) <= w - 8 ? Large : Medium;
    drawText(d, cf, 4, callerY + (Large.height - cf.height) / 2, caller, ink, p::Ground);
    const int callerIdY = callerY + Large.height + 1;
    if (known) {
        const int idX = w - 4 - textWidth(Body, callerId.view());
        drawText(d, Body, idX, callerIdY, callerId.view(), p::Dim, p::Ground);
        const std::string_view name = cut(c.callerName, (idX - 10) / Body.width);
        drawText(d, Body, 4, callerIdY, name, ink, p::Ground);
    }

    // Whom: the talkgroup's or contact's name, its number under it.
    const int ruleY = callerIdY + Body.height + 3;
    d.fillRect(4, ruleY, w - 8, 1, p::Rule);
    const int targetY = ruleY + 4;
    lib::StaticString<16> number;
    if (c.allCall) {
        number << "All";
    } else {
        number << (c.group ? "TG " : "To ") << c.targetId;
    }
    if (c.targetName.empty() || c.allCall) {
        drawText(d, Medium, 4, targetY, number.view(), ink, p::Ground);
    } else {
        drawText(d, Medium, 4, targetY, c.targetName, ink, p::Ground);
        drawText(d, Body, 4, targetY + Medium.height + 1, number.view(), p::Dim, p::Ground);
    }
    drawCallTime(d, c);
}

void StandbyScreen::drawCallTime(Display &d, const CallInfo &c)
{
    // The bottom bar: the call's time, the channel on the right.
    const int w = d.width();
    const int h = d.height();
    d.fillRect(0, h - HintH, w, 1, p::Rule);
    d.fillRect(0, h - HintH + 1, w, HintH - 1, p::Ground);
    lib::StaticString<12> t;
    t << c.seconds / 60 << ':' << static_cast<char>('0' + c.seconds % 60 / 10) << static_cast<char>('0' + c.seconds % 10);
    drawText(d, Small, 3, h - HintH + 2, t.view(), c.active ? p::Text : p::Dim, p::Ground);
    const int room = w - 6 - textWidth(Small, t.view()) - 8;
    const std::string_view channel = cut(c.channel, room / Small.width);
    drawText(d, Small, w - 3 - textWidth(Small, channel), h - HintH + 2, channel, p::Dim, p::Ground);
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
