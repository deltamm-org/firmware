// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/dmr.h"

#include <concepts>
#include <cstdint>
#include <string_view>

namespace dmm::services {

struct BasebandInfo
{
    bool running = false;
    /// Why it is not running, for the log; nullptr while running.
    const char *problem = nullptr;
    char product[16] = {};
    char hardware[16] = {};
    char software[16] = {};
    /// The chip's "data waiting" line, read when info() is called.
    bool dataWaiting = false;
};

/// Speaker volume steps: 0 = muted, 1..SpeakerVolumeSteps = quietest to
/// loudest.
inline constexpr int SpeakerVolumeSteps = 15;

enum class BasebandWorkMode : std::uint8_t
{
    Idle,
    Receive,
    Transmit,
};

/// The radio's baseband processor and the audio path behind it (codec,
/// speaker). Optional: the board names the type (dmm/board/parts.h), or
/// NoBaseband. Used by the radio task only.
///
/// - start(): powers the chip up, loads it and sets up the analog audio
///   path. Takes a few seconds. info(): how it went.
/// - setSpeakerVolume(step): 0..SpeakerVolumeSteps.
/// - playTone(hz, ms, percent): a tone on the speaker path at @p percent
///   of full scale.
/// - setAnalog(wide): analog FM; the chip filters and levels the audio in
///   both directions, with pre- and de-emphasis, for a wide or narrow
///   channel, at the radio's calibrated levels. False without a
///   calibration.
/// - setWorkMode(mode).
/// - setCarrier(present): in analog mode the chip passes receive audio
///   only while told a carrier is present.
/// - setMicrophone(on): connects the microphone for transmit, or
///   disconnects it and restores the receive levels.
/// - setDigital(settings, receiveHz, repeater): DMR on the channel's
///   colour code, at the radio's calibrated levels for @p receiveHz,
///   receiving the channel's slot through a repeater, or both slots
///   direct. False if the chip did not take it.
/// - nextEvent(event): the next thing heard on a digital channel; false
///   when there is none.
/// - startDigitalCall(settings, lc, transmitHz): transmits on the
///   channel's slot: the call's header, then voice from the microphone.
///   The caller keys the RF path. False if the chip did not take it.
/// - endDigitalCall(settings, lc): sends the call's terminator; the
///   AllSent event follows once it has gone out.
/// - wakeRepeater(settings, source): sends the control block that wakes
///   an idle repeater.
/// - debugCommand(text), debugHelp(): the chip's own console commands, for
///   experiments; results go to the log, also when start() failed. False
///   if @p text is none of them.
template <class T>
concept Baseband = requires(T &bb, const T &state, int step, std::uint16_t hz, std::uint16_t ms, int percent, bool on,
    BasebandWorkMode mode, std::string_view text, const dmr::ChannelSettings &dmrSettings, std::uint32_t receiveHz,
    dmr::Event &event, const dmr::FullLc &lc) {
    { bb.start() } -> std::same_as<bool>;
    { bb.info() } -> std::same_as<BasebandInfo>;
    { bb.setSpeakerVolume(step) } -> std::same_as<bool>;
    { bb.playTone(hz, ms, percent) } -> std::same_as<bool>;
    { bb.setAnalog(on) } -> std::same_as<bool>;
    { bb.setWorkMode(mode) } -> std::same_as<bool>;
    { bb.setCarrier(on) } -> std::same_as<bool>;
    { bb.setMicrophone(on) } -> std::same_as<bool>;
    { bb.setDigital(dmrSettings, receiveHz, on) } -> std::same_as<bool>;
    { bb.nextEvent(event) } -> std::same_as<bool>;
    { bb.startDigitalCall(dmrSettings, lc, receiveHz) } -> std::same_as<bool>;
    { bb.endDigitalCall(dmrSettings, lc) } -> std::same_as<bool>;
    { bb.wakeRepeater(dmrSettings, receiveHz) } -> std::same_as<bool>;
    { bb.debugCommand(text) } -> std::same_as<bool>;
    { state.debugHelp() } -> std::same_as<const char *>;
};

/// The board's parts name this when the radio has no baseband; code for
/// one is then left out.
struct NoBaseband
{
};

/// What a board does around the baseband chip: power and reset. Virtual,
/// so the chip's service can be tested with a stand-in board.
class BasebandBoard
{
public:
    /// Powers the chip and resets it, leaving it waiting for its boot
    /// program. Blocks for about a second.
    virtual void powerUpAndReset() = 0;

protected:
    ~BasebandBoard() = default;
};

} // namespace dmm::services
