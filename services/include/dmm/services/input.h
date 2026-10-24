// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"

#include <cstdint>
#include <string_view>

namespace dmm::services {

/// Every key a radio may have. A board without some of them never reports
/// them. The set is fixed for now; later the board could name its own keys
/// at compile time, so the count follows the radio.
enum class Key : std::uint8_t
{
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Star,
    Hash,
    Menu,
    Exit,
    Up,
    Down,
    // Programmable keys on the front panel.
    Front1,
    Front2,
    // Programmable keys on the radio's sides or top.
    Side1,
    Side2,
    Side3,
    Ptt,
    Count,
};

/// Bit n set = Key n pressed.
using KeyMask = std::uint32_t;
static_assert(static_cast<unsigned>(Key::Count) <= 32);

constexpr KeyMask keyBit(Key key)
{
    return 1u << static_cast<unsigned>(key);
}

/// Name for logs: "0", "*", "Menu", "Front1", "PTT", ... Screens use the
/// board's InputDevices::keyLabel() instead.
std::string_view keyName(Key key);

/// The board's input hardware, read by the input task only.
class InputDevices
{
public:
    /// Keys down right now, not debounced.
    virtual KeyMask readKeys() = 0;

    /// The channel knob's running position in raw counts; wraps around.
    virtual std::int32_t encoderPosition() = 0;
    /// Raw counts per click of the knob.
    virtual int encoderCountsPerDetent() const = 0;

    /// The volume knob, 0..1000, or -1 on a board without one.
    virtual int volumeKnob() = 0;
    /// The label on @p key as the user sees it, for screens; at most four
    /// characters. Constant, so any task may call it.
    virtual std::string_view keyLabel(Key key) const = 0;

protected:
    ~InputDevices() = default;
};

/// Turns raw key samples taken at a fixed rate into events.
class KeyDebouncer
{
public:
    enum class EventType : std::uint8_t
    {
        Press,
        Release,
        /// Held for LongPressMs; Release still follows.
        LongPress,
        /// While Up or Down is held, after RepeatDelayMs, every RepeatMs.
        Repeat,
    };

    struct Event
    {
        Key key;
        EventType type;
    };

    /// A change must be seen in this many samples in a row to count.
    static constexpr int StableSamples = 2;
    static constexpr os::Ticks LongPressMs = 1000;
    static constexpr os::Ticks RepeatDelayMs = 500;
    static constexpr os::Ticks RepeatMs = 150;

    /// Feeds one sample taken at @p now; calls @p onEvent for each event.
    void update(KeyMask raw, os::Ticks now, lib::FnRef<void(const Event &)> onEvent);

    /// The debounced state.
    KeyMask pressed() const { return m_stable; }

private:
    struct KeyTiming
    {
        os::Ticks pressedAt = 0;
        os::Ticks nextRepeat = 0;
        bool longSent = false;
    };

    KeyMask m_stable = 0;
    KeyMask m_candidate = 0;
    int m_candidateCount = 0;
    KeyTiming m_timing[static_cast<unsigned>(Key::Count)]{};
};

} // namespace dmm::services
