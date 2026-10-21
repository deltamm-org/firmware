// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/calibration.h"

#include <cstdint>

namespace dmm::services {

/// One FM channel.
struct FmChannel
{
    std::uint32_t receiveHz = 0;
    std::uint32_t transmitHz = 0;
    bool wide = false; // 25 kHz rather than 12.5 kHz
};

/// What the receiver hears right now, before any debouncing.
struct FmReceiveStatus
{
    bool carrier = false; // squelch decision: a signal is present
    int rssiDbm = 0;
    int noiseDb = 0;
    int quality = 0;
};

/// The analog FM transceiver and the board around it.
class FmTransceiver
{
public:
    /// True when the chip answers.
    virtual bool present() = 0;
    /// Resets and sets the chip up: idle, microphone muted, every RF
    /// switch off. False when the chip does not answer.
    virtual bool start() = 0;

    /// Tunes to @p channel's receive frequency and starts receiving, with
    /// the reference trim, front-end tuning and squelch threshold for
    /// squelch level @p squelchLevel (1..5) from @p calibration. Returns
    /// nullptr, or why it did not.
    virtual const char *receive(const FmChannel &channel, int squelchLevel, const StockCalibration &calibration) = 0;
    /// Transmits on @p channel's transmit frequency at @p level, with a
    /// CTCSS tone of @p ctcssCentiHz (0.01 Hz units; 0 = none), power and
    /// deviation from @p calibration. The microphone is unmuted. Returns
    /// nullptr, or why it did not transmit (then nothing was switched on).
    virtual const char *transmit(const FmChannel &channel, PowerLevel level, std::uint16_t ctcssCentiHz,
        const StockCalibration &calibration) = 0;
    /// Ends a transmission: power amplifier off first, then the transmit
    /// path, microphone muted, idle. Safe to call at any time.
    virtual void endTransmit() = 0;
    /// The power-amplifier control value of the running transmission, or 0.
    virtual std::uint16_t transmitPower() const = 0;

    /// Stops receiving or transmitting; the chip stays powered.
    virtual void idle() = 0;
    /// Battery save: switches the receiver and its front end off while
    /// receiving, keeping the tuning.
    virtual void sleepReceiver() = 0;
    /// Switches a sleeping receiver back on.
    virtual void wakeReceiver() = 0;
    /// True from sleepReceiver() until wakeReceiver(), receive(),
    /// transmit() or idle().
    virtual bool receiverAsleep() const = 0;
    virtual bool receiving() const = 0;
    virtual bool transmitting() const = 0;
    virtual FmReceiveStatus status() = 0;
    /// Overrides the receive front-end tuning (12 bits) until the next
    /// receive(), for measurements.
    virtual void setReceiveTune(std::uint16_t value) = 0;

    /// One register, for the console.
    virtual std::uint16_t readRegister(std::uint8_t address) = 0;
    virtual void writeRegister(std::uint8_t address, std::uint16_t value) = 0;
    /// Mutes or unmutes the microphone. Returns the chip's pin register as
    /// written, for the log.
    virtual std::uint16_t setMicrophoneMuted(bool muted) = 0;

protected:
    ~FmTransceiver() = default;
};

} // namespace dmm::services
