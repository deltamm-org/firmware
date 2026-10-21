// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/drivers/radio/rda1846.h"
#include "dmm/services/fm_transceiver.h"
#include "dmm/services/rf_hardware.h"

namespace dmm::services {

/// FmTransceiver on an RDA1846 whose GPIO pins switch the board's RF paths.
class Rda1846Transceiver final : public FmTransceiver
{
public:
    /// What the transceiver's GPIO pins do on the board (pin numbers 0..7).
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
    };

    Rda1846Transceiver(drivers::radio::Rda1846 &chip, RfHardware &rf, const Pins &pins, void (*sleepMs)(unsigned));

    bool present() override { return m_chip.present(); }
    bool start() override;
    const char *receive(const FmChannel &channel, int squelchLevel, const StockCalibration &calibration) override;
    const char *transmit(const FmChannel &channel, PowerLevel level, std::uint16_t ctcssCentiHz,
        const StockCalibration &calibration) override;
    void endTransmit() override;
    std::uint16_t transmitPower() const override { return m_power; }
    void idle() override;
    void sleepReceiver() override;
    void wakeReceiver() override;
    bool receiverAsleep() const override { return m_asleep; }
    bool receiving() const override { return m_chip.mode() == drivers::radio::Rda1846::Mode::Receive; }
    bool transmitting() const override { return m_chip.mode() == drivers::radio::Rda1846::Mode::Transmit; }
    FmReceiveStatus status() override;
    void setReceiveTune(std::uint16_t value) override { m_rf.setReceiveTune(value); }
    std::uint16_t readRegister(std::uint8_t address) override { return m_chip.readRegister(address); }
    void writeRegister(std::uint8_t address, std::uint16_t value) override { m_chip.writeRegister(address, value); }
    std::uint16_t setMicrophoneMuted(bool muted) override;

    /// The GPIO register at start: microphone muted, every RF switch and
    /// the GPS off, other pins floating.
    std::uint16_t startGpio() const;

private:
    void setPin(unsigned pin, bool high);
    unsigned frontEnd() const;

    drivers::radio::Rda1846 &m_chip;
    RfHardware &m_rf;
    Pins m_pins;
    void (*m_sleepMs)(unsigned);
    std::uint16_t m_power = 0;
    bool m_wide = false;
    std::uint32_t m_receiveHz = 0;
    bool m_asleep = false;
};

} // namespace dmm::services
