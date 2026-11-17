// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/board/parts.h"
#include "dmm/services/aprs_home.h"
#include "dmm/services/codeplug.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace dmm::app::aprs {

/// Time between automatic beacons; 0 = off.
struct BeaconInterval
{
    std::uint16_t seconds;
};
/// Transmit power for beacons; Low until set.
struct BeaconPower
{
    services::PowerLevel level;
};
/// Sends a beacon as soon as there is a position; @c here on the current
/// channel instead of the APRS frequency (console test).
struct BeaconNow
{
    bool here = false;
};
/// Logs the station and the beacon state.
struct Report
{
};
/// APRS mode is on (or off) and holds the receiver at @c hz: the received
/// audio is decoded while the radio listens there.
struct ModeOn
{
    bool on;
    std::uint32_t hz;
};

/// Decoding the received audio: in APRS mode (On, the default), never
/// (Off), or on any analog channel, in the mode or not (AnyChannel, for
/// tests).
enum class DecodeMode : std::uint8_t
{
    Off,
    On,
    AnyChannel,
};
struct Decode
{
    DecodeMode mode;
};
/// The decoder repairs frames with one wrong bit or tone (off at start).
struct RepairFrames
{
    bool on;
};
/// Logs the receive state and counts.
struct ReceiveReport
{
};

using Message = std::variant<BeaconInterval, BeaconPower, BeaconNow, Report, ModeOn, Decode, RepairFrames, ReceiveReport>;

/// A station heard, as the Heard list shows it.
struct HeardStation
{
    static constexpr std::size_t CallBytes = 9;
    static constexpr std::size_t TextBytes = 32;

    char call[CallBytes + 1] = {};
    bool hasPosition = false;
    std::int32_t latitudeE7 = 0;
    std::int32_t longitudeE7 = 0;
    char symbolTable = 0;
    char symbol = 0;
    bool hasAltitude = false;
    std::int32_t altitudeM = 0;
    bool hasCourse = false;
    std::uint16_t courseDeg = 0;
    std::uint16_t speedKnots = 0;
    /// The comment, or the text of a packet without a position.
    char text[TextBytes + 1] = {};
    /// The digipeater it came through, or empty when heard direct.
    char via[CallBytes + 1] = {};
    /// When it was last heard (os::nowMs()), and how many packets.
    std::uint32_t heardAtMs = 0;
    std::uint16_t packets = 0;
};
inline constexpr std::size_t MaxHeard = 16;

/// Starts the aprs task, which owns @p sampler (nullptr on a board without
/// one) and decodes the received audio from it. Beacons wait for
/// setStation().
void start(board::AudioSampler *sampler);

/// How many stations the heard list holds, newest first. Any task.
std::size_t heardCount();
/// The station at @p index in the heard list, or the one with @p call;
/// false if there is none. Any task.
bool heardStation(std::size_t index, HeardStation &out);
bool heardStation(std::string_view call, HeardStation &out);

/// What the APRS mode's home screen shows of receiving: the decoder runs
/// now, and the packets that reached the heard list. Any task.
struct ReceiveView
{
    bool decoding = false;
    std::uint32_t packets = 0;
};
ReceiveView receiveView();

/// Our beacon's state. Any task.
services::aprs::BeaconStatus beaconStatus();

/// Queues a message for the aprs task; false when the queue is full.
bool post(const Message &message);

/// Our station, from the codeplug, with its beacon interval and power;
/// beacons wait for it. Its fixed position is not used: beacons send the
/// GPS position. Any task.
void setStation(const services::AprsSettings &settings);

/// The console's APRS commands, for its help.
const char *consoleHelp();
/// The console's APRS receive commands, for its help.
const char *receiveHelp();

/// Runs an "aprs ..." console command; false if @p command is not one.
bool runCommand(std::string_view command);

} // namespace dmm::app::aprs
