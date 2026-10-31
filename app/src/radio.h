// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/app/app.h"
#include "dmm/lib/format.h"
#include "dmm/services/ax25.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/fm_transceiver.h"

#include <cstdint>
#include <span>
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
/// One of a chip's own console commands (console test).
struct ChipCommand
{
    enum class Chip : std::uint8_t
    {
        Fm,
        Baseband,
    };
    Chip chip;
    lib::StaticString<64> text;
};
/// Microphone on (unmuted) or muted.
struct Microphone
{
    bool on;
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
/// The transmitted voice shaped by the radio's equalizer (on) or flat.
struct VoiceEnhancement
{
    bool on;
};
/// Logs battery save's state and what keeps the radio awake.
struct BatterySaveStatus
{
};
/// Logs the FM receiver's state and signal strength.
struct FmStatus
{
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
/// The tone sent with FM transmit, or listened for on receive (the
/// speaker then opens only for it).
struct FmTone
{
    bool transmit;
    services::Tone tone;
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
/// Which microphone routes feed the transmitter (console test): both, only
/// through the baseband, or only straight to the transceiver.
struct FmTransmitAudio
{
    enum class Route : std::uint8_t
    {
        Both,
        Baseband,
        Direct,
    };
    Route route;
};
/// Works on @c channel from now on: receive, and transmit on PTT, with its
/// frequencies, width, tone and power. @c squelch is 0..5.
/// Receive DMR at @c receiveHz on colour code @c colourCode, slot 1 or 2
/// through a repeater, or both slots direct (@c slot 0).
struct DmrReceive
{
    std::uint32_t receiveHz;
    std::uint8_t colourCode;
    std::uint8_t slot;
};
/// DMR transmit for the session: whom to call (id 0 = the channel's own
/// contact) and the radio's own ID (0 = the channel's).
struct DmrTarget
{
    services::dmr::Target target;
};
struct DmrOwnId
{
    std::uint32_t id;
};
struct UseChannel
{
    services::Channel channel;
    std::uint8_t squelch;
};

/// Switches the GPS receiver's supply, which runs through the FM
/// transceiver's pins.
struct GpsPower
{
    bool on;
};

/// A frame was queued with sendPacket().
struct SendPacket
{
};
/// How packets go out: flags before the frame, and the audio level in
/// percent of the board's full level.
struct PacketSettings
{
    std::uint16_t leadMs;
    std::uint8_t levelPercent;
};

using Message = std::variant<Info, Restart, ChipTone, SpeakerVolume, ChipCommand, Microphone, FmReceive, FmIdle, FmSquelch, FmStatus, Ptt, FmTransmitFrequency, FmTone, FmPower, FmTimeout, FmTransmitAudio, DmrReceive, DmrTarget, DmrOwnId, UseChannel, BatterySave, BatterySaveStatus, VoiceEnhancement, GpsPower, SendPacket, PacketSettings>;

/// Starts the radio task, which owns @p fm, @p baseband and @p modem
/// (each nullptr on a board without one) and brings them up.
void start(board::Fm &fm, board::Baseband *baseband, board::Modem *modem);

/// Where a packet goes out: simplex on @c hz (0 = the current FM channel),
/// without a tone.
struct PacketChannel
{
    std::uint32_t hz = 0;
    bool wide = true;
    services::PowerLevel power = services::PowerLevel::Low;
};

/// Queues @p frame for @p channel. The radio tunes there if it is elsewhere, waits until
/// the channel is free, sends the frame with the microphone muted and
/// goes back to the working channel; it gives the frame up if the channel
/// stays busy, or the user transmits or changes channel meanwhile. The ui
/// hears whether it went out. False while another frame waits, or
/// without a modem.
bool sendPacket(const services::ax25::Frame &frame, const PacketChannel &channel);

/// Queues a message for the radio task; false when the queue is full or
/// the task has not started.
bool post(const Message &message);

/// What the radio is doing on the air, for the status LEDs.
enum class Activity : std::uint8_t
{
    Idle,
    FmReceive,  // the speaker is open on an FM signal
    DmrReceive, // a DMR call is heard
    Transmit,   // on the air, FM or DMR, a repeater wake-up included
};
/// Any task may ask.
Activity activity();

/// True once the radio's chips are up and it takes commands that reach the
/// air; false while they start or restart. Any task may ask.
bool ready();

/// The talkgroups heard besides the channel's own contact (the channel's
/// receive group list); set before posting the channel.
void setReceiveGroups(std::span<const std::uint32_t> ids);

} // namespace dmm::app::radio
