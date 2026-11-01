// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "draw.h"

#include "dmm/lib/format.h"
#include "dmm/services/channel_selector.h"
#include "dmm/services/display.h"
#include "dmm/services/home_screen.h"
#include "dmm/services/nmea.h"

#include <cstdint>
#include <string_view>

namespace dmm::app::ui {

/// The GPS receiver as the screens show it.
struct GpsState
{
    bool on = false;
    /// Sentences are arriving.
    bool receiving = false;
    services::GpsFix fix;
    /// Switched off between fixes to save power.
    bool asleep = false;
};

/// What the standby screen shows besides the channel.
struct StandbyState
{
    bool transmitting = false;
    bool receiving = false;
    bool locked = false;
    /// Digits typed for a frequency, or empty.
    std::string_view entry;
    /// The quick-key hint for the bottom bar.
    std::string_view keys;
    /// A short message in the bottom bar instead of the key hints, or empty.
    std::string_view note;
    /// What each row of the home screen shows.
    services::HomeLayout layout;
    /// The values that change by themselves: time, signal, voltage, volume,
    /// the last call's age and the position.
    services::HomeReadings readings;
    GpsState gps;
    /// Local time "HH:MM", or empty while the time is unknown.
    std::string_view clock;
    /// A DMR channel's contact ("TG 9", "PC 1234567", "ALL CALL") and the
    /// contact's name; empty without them.
    std::string_view talkgroup;
    std::string_view talkgroupName;
    /// The last caller's name or ID, or empty.
    std::string_view lastCaller;
    /// The channel roams: a search under way, no repeater answering a
    /// wake-up, or the repeater in use (its output frequency, 0 = the
    /// channel's own; its colour code; its place in the roaming zone from 1,
    /// 0 for none; its name, or empty).
    bool roaming = false;
    bool roamSearching = false;
    bool roamLost = false;
    std::uint32_t roamHz = 0;
    std::uint8_t roamColourCode = 0;
    int roamPosition = 0;
    std::string_view roamSite;
};

/// A DMR call for the call screen.
struct CallInfo
{
    /// Heard now; else the last call, shown for a moment after it ended.
    bool active = false;
    bool group = false;
    bool allCall = false;
    /// The caller's callsign or contact name, the user's name under it;
    /// empty when not known.
    std::string_view callerHeadline;
    std::string_view callerName;
    std::uint32_t callerId = 0;
    std::string_view targetName;
    std::uint32_t targetId = 0;
    /// The receive slot, 0 when both are heard.
    std::uint8_t slot = 0;
    /// How long the call has lasted, or lasted.
    std::uint32_t seconds = 0;
    std::string_view channel;
};

/// The bottom row's text, as @p s.layout's bottom row chooses, in at most
/// @p chars characters. Details: the repeater a roaming channel works
/// through, an FM channel's tones and offset, else the zone (or the
/// channel's name when something else is large) and the channel's place.
lib::StaticString<48> bottomText(const services::ChannelSelector &sel, const StandbyState &s, int chars);

/// The radio's main screen, the home screen: a status bar, then rows as
/// the layout sets them (a top line with the clock, a large name, a row on
/// a panel, the power with a reading beside it, a bottom row), and a bar
/// naming the quick keys.
class StandbyScreen
{
public:
    static constexpr int StatusH = 11;
    static constexpr int HintH = 11;

    void draw(services::Display &d, const services::ChannelSelector &sel, const StandbyState &s);
    /// Draws again only the rows whose values changed since the screen was
    /// last drawn; returns them (services::home_rows bits).
    std::uint8_t refresh(services::Display &d, const services::ChannelSelector &sel, const StandbyState &s);
    /// Only the status bar (transmit, receive, lock, GPS); battery is drawn
    /// by the caller at the right end.
    void drawStatus(services::Display &d, const StandbyState &s);
    /// The call screen in place of the channel: call type, caller, target,
    /// and the call's time in the bottom bar.
    void drawCall(services::Display &d, const StandbyState &s, const CallInfo &c);
    /// Only the call screen's bottom bar, for the running time.
    void drawCallTime(services::Display &d, const CallInfo &c);
    /// Where the caller may draw the battery: x of its left edge.
    int batteryX(const services::Display &d) const { return d.width() - 48; }

private:
    void drawClock(services::Display &d, const StandbyState &s);
    void drawMiddle(services::Display &d, const services::ChannelSelector &sel, const StandbyState &s);
    void drawBeside(services::Display &d, const StandbyState &s);
    void drawBottom(services::Display &d, const services::ChannelSelector &sel, const StandbyState &s);

    /// The readings the screen shows now.
    services::HomeReadings m_shown;
};

/// The GPS screen: the receiver's state, position, altitude, satellites,
/// speed and UTC time, between the status bar and a hint bar. @p full
/// draws the labels and rules too; otherwise only the values change,
/// drawn over the old ones.
void drawGpsScreen(services::Display &d, const GpsState &gps, bool full);

/// A list to pick one item from, drawn as the menu's lists are, under the
/// status bar.
class ListPicker
{
public:
    static constexpr std::size_t MaxItems = 250;
    static constexpr std::size_t TextBytes = 17;

    void open(std::string_view title);
    /// An item: @p text on the left, @p value (if any) right-aligned. Both
    /// are cut to fit their buffers: keep @p text to 16 characters and
    /// @p value to 8.
    void add(std::uint8_t id, std::string_view text, std::string_view value = {});
    void select(std::uint8_t id);
    void move(int steps);
    bool empty() const { return m_count == 0; }
    std::uint8_t chosen() const { return m_ids[m_cursor]; }
    /// The list with @p hint naming the keys.
    void draw(services::Display &d, std::string_view hint);

private:
    char m_title[TextBytes] = {};
    std::uint8_t m_ids[MaxItems] = {};
    char m_text[MaxItems][TextBytes] = {};
    static constexpr std::size_t ValueBytes = 10;
    char m_value[MaxItems][ValueBytes] = {};
    std::size_t m_count = 0;
    std::size_t m_cursor = 0;
};

} // namespace dmm::app::ui
