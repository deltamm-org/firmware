// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/app/app.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/display.h"
#include "dmm/services/input.h"
#include "dmm/services/key_lock.h"
#include "dmm/services/nmea.h"

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
/// A DMR call is heard (active) or over: who is calling and whom.
struct DmrCall
{
    bool active;
    bool group;
    std::uint32_t source;
    std::uint32_t destination;
};
/// Roaming on the current channel: a search under way, or the repeater in
/// use (@c site -1: none yet) with its output frequency and colour code.
struct Roaming
{
    bool searching;
    std::int8_t site;
    std::uint32_t receiveHz;
    std::uint8_t colourCode;
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
/// Sets battery save as Menu, Battery does (console): 0 off, 1 on, 2 fast
/// wake.
struct BatterySave
{
    std::uint8_t mode;
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
/// A short message for the user, up to 20 characters; the text must
/// stay valid (a string literal).
struct Notice
{
    const char *text;
};
/// The "switching off" screen.
struct SwitchingOff
{
};
/// The GPS receiver's state and latest fix.
struct Gps
{
    services::GpsFix fix;
    bool on;
    bool receiving;
    /// Enabled, but switched off between fixes to save power.
    bool asleep;
};

using Message = std::variant<Backlight, BacklightTime, ShowTestPattern, ShowLog, ShowKeyTest, ToggleView, PanelPower, Keys, KeyEvent, EncoderClicks,
    EncoderRaw, Volume, Battery, SwitchingOff, Transmitting, Receiving, DmrCall, Roaming, ShowStandby, SetKeyLock, BatterySave, KeyLockSettings, KeyLockReport, Notice, Gps>;

/// Starts the ui task, which owns @p display.
/// @p input (may be nullptr) gives the key labels.
void start(services::Display &display, services::CodeplugSource *codeplug, const board::Input *input);

/// Queues a message for the ui task; false when the queue is full or there
/// is no display.
bool post(const Message &message);

} // namespace dmm::app::ui
