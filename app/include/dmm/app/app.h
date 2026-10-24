// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/audio.h"
#include "dmm/services/baseband.h"
#include "dmm/services/battery.h"
#include "dmm/services/byte_channel.h"
#include "dmm/services/calibration.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/display.h"
#include "dmm/services/fm_transceiver.h"
#include "dmm/services/input.h"
#include "dmm/services/leds.h"
#include "dmm/services/log_sink.h"
#include "dmm/services/storage.h"
#include "dmm/services/system_control.h"

namespace dmm::app {

/// What a board hands the app: its service interfaces and a name for the log.
struct Platform
{
    services::Leds &leds;
    services::LogSink &logSink;
    services::SystemControl &system;
    const char *boardName;
    /// A line the board wants logged at start, or nullptr.
    const char *bootReport = nullptr;
    /// The display, or nullptr on a board without one.
    services::Display *display = nullptr;
    /// Keys and knobs, or nullptr on a board without them.
    services::InputDevices *input = nullptr;
    /// Battery voltage sense, or nullptr.
    services::BatterySensor *battery = nullptr;
    /// The radio's data flash (codeplug, calibration), or nullptr.
    services::Storage *storage = nullptr;
    /// The PC data port (DeltaMM's own protocol), or nullptr.
    services::ByteChannel *dataChannel = nullptr;
    /// The baseband processor and the codec behind it, or nullptr.
    services::Baseband *baseband = nullptr;
    /// The analog FM transceiver, or nullptr.
    services::FmTransceiver *fm = nullptr;
    /// Speaker amplifier, its receive-audio switch and beep tones, or nullptr.
    services::AudioHardware *audio = nullptr;
    /// The radio's calibration, read from storage at start, or nullptr.
    services::Calibration *calibration = nullptr;
    /// The radio's channels, zones and settings, or nullptr.
    services::CodeplugSource *codeplug = nullptr;
};

/// Creates the app's tasks. Called once, before os::runScheduler().
void start(Platform &platform);

} // namespace dmm::app
