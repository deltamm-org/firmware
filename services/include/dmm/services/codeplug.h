// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/dmr.h"
#include "dmm/services/key_lock.h"
#include "dmm/services/power_level.h"
#include "dmm/services/tone.h"

#include <array>
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
    /// Used on digital channels.
    dmr::ChannelSettings dmr;
    /// Digital: works through whichever repeater of the roaming zone is in
    /// use, which gives the frequencies and colour code.
    bool roaming = false;
    /// Up to NameBytes bytes, zero-terminated.
    char name[NameBytes + 1] = {};

    /// Transmits and receives FM.
    bool analog() const { return mode == ChannelMode::Analog || mode == ChannelMode::AnalogDigital; }
    /// Transmits and receives DMR.
    bool digital() const { return !analog(); }
};

/// What a programmable key does.
enum class KeyFunction : std::uint8_t
{
    None,
    Roam, // look for the best repeater of the roaming zone now
};

/// Roaming: the repeaters roaming channels use, and when the radio looks
/// for a better one by itself.
struct RoamingSettings
{
    static constexpr std::uint8_t NoZone = 0xFF;

    enum class Start : std::uint8_t
    {
        Periodic,   // every periodMinutes
        OutOfRange, // when the repeater in use has not been heard for quietS
    };

    /// The roaming zone, or NoZone.
    std::uint8_t zone = NoZone;
    /// Looks by itself; else only on a key, the menu or a failed PTT.
    bool automatic = false;
    Start start = Start::OutOfRange;
    std::uint16_t periodMinutes = 0;
    std::uint16_t quietS = 0;
    /// Checks the repeater in use, every checkIntervalS while it is not
    /// heard, with a wake-up; checkTries unanswered in a row put it out of
    /// range.
    bool rangeCheck = false;
    std::uint8_t checkIntervalS = 5;
    std::uint8_t checkTries = 3;
    /// How many times the user is told the repeater is out of range; 0 =
    /// never.
    std::uint8_t outOfRangeNotices = 0;
};

/// The general settings DeltaMM uses so far.
struct CodeplugSettings
{
    static constexpr std::size_t SideKeys = 3;

    std::uint8_t squelchA = 3; // 0 = open, 1..5
    std::uint8_t squelchB = 3;
    std::uint16_t transmitTimeoutS = 0; // 0 = none
    bool vfoA = false;                  // side A tunes freely rather than from channels
    bool vfoB = false;
    std::uint8_t zoneA = 0; // zone index of each side
    std::uint8_t zoneB = 0;
    bool sideBMain = false; // side B is the main one
    KeyLockSettings keyLock;
    std::uint8_t batterySave = 0;     // 0 = off; 1 or 2 = the receiver sleeps 150 or 70 ms per cycle
    std::uint16_t backlightS = 0;     // backlight time after the last key; 0 = always on
    bool voiceEnhancement = false;    // transmit voice shaped by the radio's equalizer
    bool gps = false;                 // GPS receiver on
    std::uint8_t gpsSystems = 3;      // satellite systems, gps_systems bits
    std::int16_t timeZoneMinutes = 0; // local time minus UTC
    RoamingSettings roaming;
    /// The side keys' functions on a short and a long press.
    KeyFunction sideShort[SideKeys] = {};
    KeyFunction sideLong[SideKeys] = {};
};

/// One repeater of a roaming zone.
struct RoamingSite
{
    /// The colour code is the channel's own.
    static constexpr std::uint8_t ChannelColourCode = 0xFF;

    std::uint32_t receiveHz = 0;  // the repeater's output
    std::uint32_t transmitHz = 0; // its input
    std::uint8_t colourCode = ChannelColourCode;
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
    static constexpr std::size_t ContactNameBytes = 16;

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
    /// The name of the DMR contact with @p id, a talkgroup when @p group,
    /// zero-terminated; false when there is none.
    virtual bool contactName(std::uint32_t id, bool group, std::span<char, ContactNameBytes + 1> name) = 0;
    static constexpr std::size_t CallsignBytes = 8;
    /// A radio user from the codeplug's user directory.
    struct DmrUser
    {
        std::array<char, CallsignBytes + 1> callsign{};
        std::array<char, ContactNameBytes + 1> name{};
    };
    /// Looks up the user with radio ID @p id in the user directory; false
    /// when there is none.
    virtual bool dmrUser(std::uint32_t id, DmrUser &user) = 0;
    static constexpr std::size_t MaxReceiveGroups = 64;
    /// The talkgroup IDs in receive group list @p list; returns how many.
    virtual std::size_t receiveGroups(std::uint8_t list, std::span<std::uint32_t> out) = 0;
    static constexpr std::size_t MaxRoamingSites = 64;
    static constexpr std::size_t SiteNameBytes = 16;
    static constexpr std::size_t MaxRoamingZones = 64;
    /// Every roaming zone in use, in order; returns how many.
    virtual std::size_t roamingZones(std::span<std::uint8_t> out) = 0;
    /// The roaming zone's name, zero-terminated; false when there is none.
    virtual bool roamingZoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name) = 0;
    /// The repeaters of roaming zone @p zone, in order; returns how many.
    virtual std::size_t roamingSites(std::uint8_t zone, std::span<RoamingSite> out) = 0;
    /// The name of the repeater at @p position in roaming zone @p zone,
    /// zero-terminated; false when there is none.
    virtual bool roamingSiteName(std::uint8_t zone, std::size_t position, std::span<char, SiteNameBytes + 1> name) = 0;

protected:
    ~CodeplugSource() = default;
};

} // namespace dmm::services
