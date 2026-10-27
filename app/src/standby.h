// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/channel_selector.h"
#include "dmm/services/display.h"

#include <cstdint>
#include <string_view>

namespace dmm::app::ui {

/// The palette of the radio's own screens: black ground, white primary
/// text, grey secondary text, thin grey rules between areas, and colour
/// only for state.
namespace palette {
inline constexpr services::Color Ground = services::rgb(0, 0, 0);
inline constexpr services::Color Text = services::rgb(255, 255, 255);
inline constexpr services::Color Dim = services::rgb(150, 150, 150);
inline constexpr services::Color Rule = services::rgb(70, 70, 70);
inline constexpr services::Color Transmit = services::rgb(210, 40, 30);
inline constexpr services::Color Receive = services::rgb(40, 170, 60);
inline constexpr services::Color Warn = services::rgb(230, 160, 0);
} // namespace palette

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
    /// Volume step 0..15, or -1 when unknown.
    int volume = -1;
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

/// The radio's main screen: status bar, zone, channel name, frequency,
/// details, and a bar naming the quick keys.
class StandbyScreen
{
public:
    static constexpr int StatusH = 11;
    static constexpr int HintH = 11;

    void draw(services::Display &d, const services::ChannelSelector &sel, const StandbyState &s);
    /// Only the status bar (transmit, receive, lock); battery is drawn by
    /// the caller at the right end.
    void drawStatus(services::Display &d, const StandbyState &s);
    /// The call screen in place of the channel: call type, caller, target,
    /// and the call's time in the bottom bar.
    void drawCall(services::Display &d, const StandbyState &s, const CallInfo &c);
    /// Only the call screen's bottom bar, for the running time.
    void drawCallTime(services::Display &d, const CallInfo &c);
    /// Where the caller may draw the battery: x of its left edge.
    int batteryX(const services::Display &d) const { return d.width() - 48; }
};

/// A list to pick one item from, drawn over the screen body: a title bar
/// and up to Rows rows, the chosen one inverted.
class ListPicker
{
public:
    static constexpr int Rows = 7;
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
    void draw(services::Display &d, int top);

private:
    char m_title[TextBytes] = {};
    std::uint8_t m_ids[MaxItems] = {};
    char m_text[MaxItems][TextBytes] = {};
    static constexpr std::size_t ValueBytes = 9;
    char m_value[MaxItems][ValueBytes] = {};
    std::size_t m_count = 0;
    std::size_t m_cursor = 0;
};

} // namespace dmm::app::ui
