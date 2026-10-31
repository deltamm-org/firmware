// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/board/parts.h"
#include "dmm/services/codeplug.h"

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
/// Receiving through the board: on powers it and reads what it hears.
struct Receive
{
    bool on;
};

using Message = std::variant<BeaconInterval, BeaconPower, BeaconNow, Report, Receive>;

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

/// Starts the aprs task, which owns @p link (nullptr on a board without
/// one). Beacons wait for a Station message, receiving for Receive.
void start(board::AprsLink *link);

/// How many stations the heard list holds, newest first. Any task.
std::size_t heardCount();
/// The station at @p index in the heard list, or the one with @p call;
/// false if there is none. Any task.
bool heardStation(std::size_t index, HeardStation &out);
bool heardStation(std::string_view call, HeardStation &out);

/// Queues a message for the aprs task; false when the queue is full.
bool post(const Message &message);

/// Our station, from the codeplug; beacons wait for it. Its fixed position
/// and power are not used: beacons send the GPS position at the power
/// BeaconPower sets. Any task.
void setStation(const services::AprsSettings &settings);

/// The console's APRS commands, for its help.
const char *consoleHelp();

/// Runs an "aprs ..." console command; false if @p command is not one.
bool runCommand(std::string_view command);

} // namespace dmm::app::aprs
