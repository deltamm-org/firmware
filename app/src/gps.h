// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/board/parts.h"
#include "dmm/lib/format.h"
#include "dmm/services/gps_schedule.h"

#include <cstdint>
#include <variant>

namespace dmm::app::gps {

/// Enables or disables the GPS. While enabled, the receiver runs whenever
/// a client needs positions often, and sleeps between fixes otherwise.
struct Power
{
    bool on;
};
/// Off: the receiver stays on whenever the GPS is enabled (battery save off).
struct PowerSave
{
    bool on;
};
/// How often @c client needs a fresh position, in milliseconds; 0 = none.
/// Needs of three minutes or less keep the receiver on; longer ones let it
/// sleep and wake in time for the next fix. A client sends a new need
/// whenever it wants positions more or less often; a shorter one takes
/// effect at once, also while the receiver sleeps.
struct Need
{
    services::GpsSchedule::Client client;
    std::uint32_t intervalMs;
};
/// Logs the receiver's state and the latest fix.
struct Report
{
};
/// Logs every sentence the receiver sends (console test).
struct Raw
{
    bool on;
};
/// The satellite systems to use (services::gps_systems bits).
struct Systems
{
    std::uint8_t systems;
};
/// One of the receiver chip's own console commands (console test).
struct ChipCommand
{
    lib::StaticString<64> text;
};

/// Measures the clock's rate against GPS time for @p minutes, keeping the
/// receiver on, then corrects the rate; 0 stops a run. Needs the GPS on.
struct CalibrateClock
{
    std::uint8_t minutes;
};

using Message = std::variant<Power, PowerSave, Need, Report, Raw, Systems, ChipCommand, CalibrateClock>;

/// Starts the gps task, which owns @p port. The receiver stays off until
/// a Power message.
void start(board::Gps &port);

/// The receiver chip's console commands, for the console's help.
const char *debugHelp();

/// Queues a message for the gps task; false when the queue is full or there
/// is no receiver.
bool post(const Message &message);

} // namespace dmm::app::gps
