// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/app/app.h"
#include "dmm/services/calibration.h"

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

/// The radio's calibration once it has been read and looks valid, else
/// nullptr. Read-only after that, from any task.
const services::Calibration *calibration();

} // namespace dmm::app::system
