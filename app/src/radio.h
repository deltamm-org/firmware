// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/baseband.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/fm_transceiver.h"

#include <cstdint>
#include <variant>

namespace dmm::app::radio {

/// Logs the baseband state.
struct Info
{
};
/// Powers the baseband off and starts it again.
struct Restart
{
};
/// A tone from the baseband itself, through the codec (console test).
struct ChipTone
{
    std::uint16_t hz;
    std::uint16_t ms;
};
/// Receive-audio level in the codec, 0 (muted)..15.
struct SpeakerVolume
{
    std::uint8_t step;
};
/// One audio codec register (console test).
struct CodecWrite
{
    std::uint8_t page;
    std::uint8_t reg;
    std::uint8_t value;
};
/// A raw baseband packet; the replies go to the log (console test).
struct RawPacket
{
    std::uint8_t type;
    std::uint8_t count;
    std::uint8_t fields[16];
};
/// Logs one FM transceiver register.
struct FmRead
{
    std::uint8_t reg;
};
/// Microphone on (unmuted) or muted.
struct Microphone
{
    bool on;
};
/// Writes one FM transceiver register (console test).
struct FmWrite
{
    std::uint8_t reg;
    std::uint16_t value;
};
/// Receive FM at @c receiveHz; the squelch opens the speaker.
struct FmReceive
{
    std::uint32_t receiveHz;
    bool wide;
};
/// Stops FM receive.
struct FmIdle
{
};
/// Squelch level: 0 = always open, 1..5 = tighter and tighter.
struct FmSquelch
{
    std::uint8_t level;
};
/// Battery save: 0 = off; 1 or 2 = the receiver sleeps 150 or 70 ms per
/// cycle while the radio is idle.
struct BatterySave
{
    std::uint8_t mode;
};
/// Logs battery save's state and what keeps the radio awake.
struct BatterySaveStatus
{
};
/// Logs the FM receiver's state and signal strength.
struct FmStatus
{
};
/// Steps the receive front-end tuning over its range and logs the signal
/// strength at each step, then puts the calibrated value back.
struct FmSweep
{
};
/// Holds the receive front-end tuning at @c value; -1 = calibrated value.
struct FmTune
{
    std::int16_t value;
};
/// The PTT key went down (transmit) or up (back to receive).
struct Ptt
{
    bool down;
};
/// Transmit frequency for the FM channel; 0 = the receive frequency.
struct FmTransmitFrequency
{
    std::uint32_t hz;
};
/// CTCSS tone sent with FM transmit, in 0.1 Hz; 0 = none.
struct FmCtcss
{
    std::uint16_t deciHz;
};
/// FM transmit power level.
struct FmPower
{
    services::PowerLevel level;
};
/// Longest FM transmission, in seconds; 0 = no limit.
struct FmTimeout
{
    std::uint16_t seconds;
};
/// Works on @c channel from now on: receive, and transmit on PTT, with its
/// frequencies, width, tone and power. @c squelch is 0..5.
struct UseChannel
{
    services::Channel channel;
    std::uint8_t squelch;
};

using Message = std::variant<Info, Restart, ChipTone, SpeakerVolume, CodecWrite, RawPacket, FmRead, Microphone, FmWrite,
    FmReceive, FmIdle, FmSquelch, FmStatus, FmSweep, FmTune, Ptt, FmTransmitFrequency, FmCtcss, FmPower, FmTimeout, UseChannel, BatterySave, BatterySaveStatus>;

/// Starts the radio task, which owns @p baseband and @p fm (may be
/// nullptr) and brings them up.
void start(services::Baseband &baseband, services::FmTransceiver *fm);

/// Queues a message for the radio task; false when the queue is full or
/// there is no baseband.
bool post(const Message &message);

} // namespace dmm::app::radio
