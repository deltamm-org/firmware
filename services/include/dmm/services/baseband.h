// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>
#include <span>
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

/// The radio's baseband processor and the audio path behind it (codec,
/// speaker). Used by the radio task only.
class Baseband
{
public:
    /// Powers the chip up, loads it and sets up the analog audio path.
    /// Takes a few seconds.
    virtual bool start() = 0;
    virtual BasebandInfo info() = 0;

    /// Speaker volume: 0 = muted, 1..VolumeSteps = quietest..loudest.
    static constexpr int VolumeSteps = 15;
    virtual bool setSpeakerVolume(int step) = 0;

    /// Plays a tone of @p hz for @p ms on the speaker path at @p percent of
    /// full scale.
    virtual bool playTone(std::uint16_t hz, std::uint16_t ms, int percent) = 0;

    /// Analog FM: the chip filters and levels the audio in both directions,
    /// with pre- and de-emphasis, for a @p wide or narrow channel, at the
    /// radio's calibrated levels. False without a calibration.
    virtual bool setAnalog(bool wide) = 0;

    enum class WorkMode : std::uint8_t
    {
        Idle,
        Receive,
        Transmit,
    };
    virtual bool setWorkMode(WorkMode mode) = 0;

    /// Tells the chip a carrier is present (true) or gone; in analog mode
    /// it passes receive audio only while one is.
    virtual bool setCarrier(bool present) = 0;

    /// Connects the microphone for transmit (true) or disconnects it and
    /// restores the receive audio levels.
    virtual bool setMicrophone(bool on) = 0;

    /// Runs one of the chip's own console commands, for experiments; the
    /// result goes to the log. Works whether or not start() succeeded.
    /// False if @p command is not one of them.
    virtual bool debugCommand(std::string_view command) = 0;
    /// The chip's console commands, for the help text.
    virtual const char *debugHelp() const = 0;

protected:
    ~Baseband() = default;
};

/// What a board does around the baseband chip: power and reset.
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
