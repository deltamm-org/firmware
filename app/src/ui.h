// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/display.h"
#include "dmm/services/input.h"
#include "dmm/services/key_lock.h"
#include "dmm/services/storage.h"

#include <cstdint>
#include <variant>

namespace dmm::app::ui {

struct Backlight
{
    std::uint8_t level; // 0..5
};
/// How long the backlight stays on after the last key; 0 = always on.
struct BacklightTime
{
    std::uint16_t seconds;
};
struct ShowTestPattern
{
};
struct ShowLog
{
};
struct ShowKeyTest
{
};
/// Between the key test and the log.
struct ToggleView
{
};
struct PanelPower
{
    bool on;
};
/// The debounced keys held now.
struct Keys
{
    services::KeyMask mask;
};
/// A key was pressed, released, held or repeated.
struct KeyEvent
{
    services::Key key;
    services::KeyDebouncer::EventType type;
};
/// The radio started (true) or stopped transmitting.
struct Transmitting
{
    bool on;
};
/// The squelch opened (true) or closed.
struct Receiving
{
    bool on;
};
/// Shows the radio's main screen.
struct ShowStandby
{
};
/// Locks or unlocks the keys (console).
struct SetKeyLock
{
    bool locked;
};
/// What the key lock covers.
struct KeyLockSettings
{
    services::KeyLockSettings settings;
};
/// Logs the key lock's state and settings.
struct KeyLockReport
{
};
struct EncoderClicks
{
    std::int32_t clicks; // since start
};
struct EncoderRaw
{
    std::int32_t raw;
};
struct Volume
{
    std::int16_t permille; // 0..1000, -1 = no knob
};
struct Battery
{
    std::uint16_t millivolts;
    std::uint8_t level;
    bool low;
};
/// The "switching off" screen.
struct SwitchingOff
{
};

using Message = std::variant<Backlight, BacklightTime, ShowTestPattern, ShowLog, ShowKeyTest, ToggleView, PanelPower, Keys, KeyEvent, EncoderClicks,
    EncoderRaw, Volume, Battery, SwitchingOff, Transmitting, Receiving, ShowStandby, SetKeyLock, KeyLockSettings, KeyLockReport>;

/// Starts the ui task, which owns @p display.
void start(services::Display &display, services::Storage *storage);

/// Queues a message for the ui task; false when the queue is full or there
/// is no display.
bool post(const Message &message);

} // namespace dmm::app::ui
