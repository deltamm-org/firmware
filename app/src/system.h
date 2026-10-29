// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/app/app.h"

#include <cstdint>
#include <optional>

namespace dmm::app::system {

/// Starts the system task: power, watchdog, battery, auto power-off.
void start(Platform &platform);

/// A key or knob was used: restarts the auto power-off countdown.
void noteActivity();

/// How long since a key or knob was last used.
os::Ticks idleForMs();

/// Minutes without activity before the radio switches itself off; 0 = never.
void setAutoPowerOffMinutes(int minutes);

/// Stops feeding the watchdog, so it restarts the radio (console test).
void stopFeedingWatchdog();

/// Logs the battery state now.
void reportBattery();

/// UTC seconds since 1970-01-01 from the real-time clock, or nothing while
/// the time is unknown or there is no clock. From any task.
std::optional<std::uint32_t> utcNow();

/// Sets the clock to @p utc; the system task applies it within a few
/// milliseconds and logs it, naming @p source (a string literal).
void setClock(std::uint32_t utc, const char *source);

/// Logs the clock's state now.
void reportClock();

/// The radio's calibration once it has been read and looks valid, else
/// nullptr. Read-only after that, from any task.
const board::Calibration *calibration();

} // namespace dmm::app::system
