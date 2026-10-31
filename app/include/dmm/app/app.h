// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/board/parts.h"
#include "dmm/services/audio.h"
#include "dmm/services/baseband.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/display.h"
#include "dmm/services/leds.h"
#include "dmm/services/log_sink.h"
#include "dmm/services/storage.h"
#include "dmm/services/system_control.h"

#include <type_traits>

namespace dmm::app {

/// What a board hands the app: its parts and a name for the log. The
/// board's parts header names the types of the concrete ones (board::...).
struct Platform
{
    services::Leds &leds;
    services::LogSink &logSink;
    services::SystemControl &system;
    /// The analog FM transceiver.
    board::Fm &fm;
    const char *boardName;
    /// A line the board wants logged at start, or nullptr.
    const char *bootReport = nullptr;
    /// The display, or nullptr on a board without one.
    services::Display *display = nullptr;
    /// Keys and knobs, or nullptr on a board without them.
    board::Input *input = nullptr;
    /// Battery voltage sense, or nullptr.
    board::Battery *battery = nullptr;
    /// The radio's data flash (codeplug, calibration), or nullptr.
    services::Storage *storage = nullptr;
    /// The PC data port (DeltaMM's own protocol), or nullptr.
    board::DataChannel *dataChannel = nullptr;
    /// The baseband processor and the codec behind it; nullptr on a board
    /// whose parts name NoBaseband.
    board::Baseband *baseband = nullptr;
    /// Speaker amplifier, its receive-audio switch and beep tones, or nullptr.
    services::AudioHardware *audio = nullptr;
    /// The radio's calibration, read from storage at start, or nullptr.
    board::Calibration *calibration = nullptr;
    /// The radio's channels, zones and settings, or nullptr.
    services::CodeplugSource *codeplug = nullptr;
    /// The GPS receiver's serial link, or nullptr.
    board::Gps *gps = nullptr;
    /// The real-time clock, or nullptr.
    board::Rtc *rtc = nullptr;
    /// The packet radio modem, or nullptr.
    board::Modem *modem = nullptr;
    /// The link to a board that decodes received APRS, or nullptr.
    board::AprsLink *aprsLink = nullptr;
};

/// The board has a baseband processor.
inline constexpr bool HasBaseband = !std::is_same_v<board::Baseband, services::NoBaseband>;

/// Runs @p f with @p baseband on boards that have one; elsewhere the call
/// is not built at all. @p f takes the baseband as auto &.
template <class F>
void withBaseband(board::Baseband *baseband, F &&f)
{
    if constexpr (HasBaseband) {
        if (baseband != nullptr) {
            f(*baseband);
        }
    }
}

/// Creates the app's tasks. Called once, before os::runScheduler().
void start(Platform &platform);

} // namespace dmm::app
