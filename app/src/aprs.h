// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/codeplug.h"

#include <cstdint>
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
using Message = std::variant<BeaconInterval, BeaconPower, BeaconNow, Report>;

/// Starts the aprs task. Beacons wait for setStation().
void start();

/// Queues a message for the aprs task; false when the queue is full.
bool post(const Message &message);

/// Our station, from the codeplug, with its beacon interval and power;
/// beacons wait for it. Its fixed position is not used: beacons send the
/// GPS position. Any task.
void setStation(const services::AprsSettings &settings);

/// The console's APRS commands, for its help.
const char *consoleHelp();

/// Runs an "aprs ..." console command; false if @p command is not one.
bool runCommand(std::string_view command);

} // namespace dmm::app::aprs
