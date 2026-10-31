// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/app/app.h"
#include "dmm/lib/format.h"

#include <cstdint>
#include <optional>

namespace dmm::app::system {

/// Starts the system task: power, watchdog, battery, auto power-off.
void start(Platform &platform);

/// A key or knob was used: restarts the auto power-off countdown.
void noteActivity();

/// Minutes without activity before the radio switches itself off; 0 = never.
void setAutoPowerOffMinutes(int minutes);

/// Stops feeding the watchdog, so it restarts the radio (console test).
void stopFeedingWatchdog();

/// Logs the battery state now.
void reportBattery();

/// UTC seconds since 1970-01-01 from the real-time clock, or nothing while
/// the time is unknown or there is no clock. From any task.
std::optional<std::uint32_t> utcNow();

/// The same in milliseconds, to within a few: the clock's last reading,
/// carried on by the system tick.
std::optional<std::int64_t> utcNowMs();

/// Sets the clock to @p utc; the system task applies it within a few
/// milliseconds and logs it, naming @p source (a string literal).
void setClock(std::uint32_t utc, const char *source);

/// Logs the clock's state now.
void reportClock();

/// Runs the clock faster by @p ppb parts per billion (negative: slower)
/// from now on, as near as the clock can; the system task applies and logs
/// it. The clock keeps it while the radio is off.
void setClockRate(std::int32_t ppb);
/// The rate correction in effect, in parts per billion.
std::int32_t clockRate();
/// "-31.5 ppm".
lib::StaticString<12> ppmText(std::int32_t ppb);
/// The same rate as seconds gained or lost in a day: "-2.7 s".
lib::StaticString<12> perDayText(std::int32_t ppb);

/// The radio's calibration once it has been read and looks valid, else
/// nullptr. Read-only after that, from any task.
const board::Calibration *calibration();

} // namespace dmm::app::system
