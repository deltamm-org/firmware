// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/drivers/radio/rda1846.h"
#include "dmm/services/fm_transceiver.h"
#include "dmm/services/rf_hardware.h"

#include <optional>

namespace dmm::services {

/// An FmTransceiver on an RDA1846 whose GPIO pins switch the board's RF
/// paths.
class Rda1846Transceiver
{
public:
    /// What the transceiver's GPIO pins do on the board (pin numbers 0..7),
    /// and the range of the board's receive front-end tuning.
    struct Pins
    {
        unsigned microphoneMute;   // high = muted
        unsigned receiveHighBand;  // receive front end above splitHz
        unsigned receiveLowBand;   // receive front end at or below splitHz
        unsigned transmitHighBand; // transmit path above splitHz
        unsigned transmitMidBand;  // transmit filter for midLowHz..splitHz
        unsigned gpsPower;         // high = on
        std::uint32_t splitHz;
        std::uint32_t midLowHz;
        std::uint16_t receiveTuneMax;
    };

    /// The noise level, in the chip's dB, the received noise must fall below
    /// for the squelch to open. Each squelch level (1..5) needs 2 dB less;
    /// wide channels carry more noise.
    static constexpr int squelchOpenNoiseDb(bool wide, int level) { return (wide ? 62 : 52) - 2 * level; }

    /// Picks the chip's register values when it starts; may read storage.
    using TuningSource = const drivers::radio::Rda1846::Tuning &(*)();

    /// The radio's calibrated values for receiving at one frequency, in the
    /// units RfHardware takes.
    struct ReceiveTrims
    {
        std::uint16_t referenceTrim;
        std::uint16_t frontEndTune;
        /// The squelch opens at this level, in dBm.
        int squelchOpenDbm;
    };
    /// The radio's calibrated values for transmitting at one frequency and
    /// power level: RfHardware units, and the chip's deviation settings for
    /// voice (10 bits) and for a CTCSS or DCS tone (6 bits).
    struct TransmitTrims
    {
        std::uint16_t referenceTrim;
        std::uint16_t powerControl;
        std::uint16_t voiceDeviation;
        std::uint8_t ctcssDeviation;
        std::uint8_t dcsDeviation;
    };
    /// The board's calibrated values; nothing for a frequency outside the
    /// calibrated bands, or before the calibration has been read.
    struct TrimsSource
    {
        std::optional<ReceiveTrims> (*receive)(std::uint32_t hz, int squelchLevel);
        std::optional<TransmitTrims> (*transmit)(std::uint32_t hz, PowerLevel level);
    };

    /// @p tuning: the board's register values, or nullptr to keep the
    /// chip's own.
    Rda1846Transceiver(drivers::radio::Rda1846 &chip, RfHardware &rf, const Pins &pins, void (*sleepMs)(unsigned),
        TrimsSource trims, TuningSource tuning = nullptr);

    bool present() { return m_chip.present(); }
    bool start();
    const char *receive(const FmChannel &channel, int squelchLevel);
    const char *transmit(const FmChannel &channel, PowerLevel level);
    void endTransmit();
    /// The power-amplifier control value of the running transmission, or 0.
    std::uint16_t transmitPower() const { return m_power; }
    void idle();
    void sleepReceiver();
    void wakeReceiver();
    bool receiverAsleep() const { return m_asleep; }
    bool receiving() const { return m_chip.mode() == drivers::radio::Rda1846::Mode::Receive; }
    bool transmitting() const { return m_chip.mode() == drivers::radio::Rda1846::Mode::Transmit; }
    FmReceiveStatus status();
    void logStatus(std::string_view what);
    /// Also logs the chip's pin register, as a debug line.
    void setMicrophoneMuted(bool muted);
    /// "<reg> [value]" (hex), "tune <value>|auto", "sweep".
    bool debugCommand(std::string_view command);
    const char *debugHelp() const;

    /// The GPIO register at start: microphone muted, every RF switch and
    /// the GPS off, other pins floating.
    std::uint16_t startGpio() const;

private:
    void setPin(unsigned pin, bool high);
    std::uint16_t muteMicrophone(bool muted);
    unsigned frontEnd() const;
    int settledRssi();
    void sweep();

    drivers::radio::Rda1846 &m_chip;
    RfHardware &m_rf;
    Pins m_pins;
    void (*m_sleepMs)(unsigned);
    TrimsSource m_trims;
    TuningSource m_tuning;
    std::uint16_t m_power = 0;
    bool m_wide = false;
    std::uint32_t m_receiveHz = 0;
    std::uint16_t m_tune = 0; // the calibrated front-end tuning in use
    bool m_asleep = false;
};

} // namespace dmm::services
