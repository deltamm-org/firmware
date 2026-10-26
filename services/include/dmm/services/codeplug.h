// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/key_lock.h"
#include "dmm/services/power_level.h"
#include "dmm/services/tone.h"

#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services {

enum class ChannelMode : std::uint8_t
{
    Analog,
    Digital,
    AnalogDigital, // analog, digital calls heard too
    DigitalAnalog, // digital, analog calls heard too
};

/// What opens the speaker on an FM channel. Stored formats map their other
/// modes (selective calling and the like) to the nearest of these.
enum class SquelchMode : std::uint8_t
{
    Carrier, // any carrier
    Tone,    // the channel's receive tone
};

/// One memory channel of the codeplug.
struct Channel
{
    static constexpr std::size_t NameBytes = 16;

    std::uint32_t receiveHz = 0;
    std::uint32_t transmitHz = 0;
    ChannelMode mode = ChannelMode::Analog;
    PowerLevel power = PowerLevel::Low;
    bool wide = false;
    bool receiveOnly = false;
    Tone receiveTone;
    Tone transmitTone;
    SquelchMode squelchMode = SquelchMode::Carrier;
    /// Up to NameBytes bytes, zero-terminated.
    char name[NameBytes + 1] = {};

    /// Transmits and receives FM.
    bool analog() const { return mode == ChannelMode::Analog || mode == ChannelMode::AnalogDigital; }
};

/// The general settings DeltaMM uses so far.
struct CodeplugSettings
{
    std::uint8_t squelchA = 3; // 0 = open, 1..5
    std::uint8_t squelchB = 3;
    std::uint16_t transmitTimeoutS = 0; // 0 = none
    bool vfoA = false;                  // side A tunes freely rather than from channels
    bool vfoB = false;
    std::uint8_t zoneA = 0; // zone index of each side
    std::uint8_t zoneB = 0;
    bool sideBMain = false; // side B is the main one
    KeyLockSettings keyLock;
    std::uint8_t batterySave = 0; // 0 = off; 1 or 2 = the receiver sleeps 150 or 70 ms per cycle
    std::uint16_t backlightS = 0; // backlight time after the last key; 0 = always on
};

/// What the radio reads from its codeplug, whatever its format. The
/// limits are the most DeltaMM handles.
class CodeplugSource
{
public:
    static constexpr std::uint16_t MaxChannels = 4000;
    static constexpr std::uint8_t MaxZones = 250;
    static constexpr std::uint8_t MaxZoneChannels = 250;
    static constexpr std::size_t ZoneNameBytes = 16;
    static constexpr std::uint16_t NoChannel = 0xFFFF;

    virtual std::optional<CodeplugSettings> settings() = 0;
    /// Channel @p index (0-based), if it is in use.
    virtual std::optional<Channel> channel(std::uint16_t index) = 0;
    /// The VFO of side A or B: a channel record whose frequency and
    /// settings the VFO starts from.
    virtual std::optional<Channel> vfo(bool sideB) = 0;
    virtual bool zoneInUse(std::uint8_t zone) = 0;
    /// Every zone in use, in order; returns how many.
    virtual std::size_t zonesInUse(std::span<std::uint8_t> out) = 0;
    /// The zone's name, zero-terminated; false if unreadable.
    virtual bool zoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name) = 0;
    /// The zone's channel indices in order; returns how many.
    virtual std::size_t zoneChannels(std::uint8_t zone, std::span<std::uint16_t> out) = 0;
    /// The position in @p zone's channel list last selected on side A or
    /// B, or NoChannel.
    virtual std::uint16_t zoneSelection(std::uint8_t zone, bool sideB) = 0;

protected:
    ~CodeplugSource() = default;
};

} // namespace dmm::services
