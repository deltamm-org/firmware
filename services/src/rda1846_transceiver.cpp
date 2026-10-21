// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/rda1846_transceiver.h"

#include "dmm/services/squelch_gate.h"

namespace dmm::services {

namespace {

using Chip = drivers::radio::Rda1846;
using Mode = Chip::GpioMode;

// The squelch closes this much below where it opens.
constexpr int SquelchHysteresisDb = 3;

// A signal this clean counts as present whatever the squelch flag says.
constexpr int StrongQualityNarrow = 6;
constexpr int StrongQualityWide = 10;

// Transmit start: the transceiver settles before the power amplifier is
// enabled, and the amplifier settles before its output rises, in three
// steps.
constexpr unsigned TransmitSettleMs = 100;
constexpr unsigned AmplifierSettleMs = 10;
constexpr unsigned PowerStepMs = 1;

} // namespace

Rda1846Transceiver::Rda1846Transceiver(Chip &chip, RfHardware &rf, const Pins &pins, void (*sleepMs)(unsigned))
    : m_chip(chip), m_rf(rf), m_pins(pins), m_sleepMs(sleepMs)
{
}

std::uint16_t Rda1846Transceiver::startGpio() const
{
    auto field = [](unsigned pin, Mode mode) { return static_cast<std::uint16_t>(static_cast<unsigned>(mode) << (2 * pin)); };
    std::uint16_t v = field(m_pins.microphoneMute, Mode::High);
    for (unsigned pin : {m_pins.receiveHighBand, m_pins.receiveLowBand, m_pins.transmitHighBand, m_pins.transmitMidBand,
             m_pins.gpsPower}) {
        v = static_cast<std::uint16_t>(v | field(pin, Mode::Low));
    }
    return v;
}

void Rda1846Transceiver::setPin(unsigned pin, bool high)
{
    m_chip.setGpio(pin, high ? Mode::High : Mode::Low);
}

bool Rda1846Transceiver::start()
{
    m_rf.setPowerControl(0);
    m_rf.setPowerAmplifier(false);
    m_rf.setTransmitPath(false);
    return m_chip.start(m_sleepMs, startGpio());
}

const char *Rda1846Transceiver::receive(const FmChannel &channel, int squelchLevel, const StockCalibration &calibration)
{
    const std::uint32_t hz = channel.receiveHz;
    const auto trim = calibration.referenceTrim();
    const auto tune = calibration.rxTune(hz);
    const auto threshold = calibration.squelchThreshold(hz, squelchLevel);
    if (!trim || !tune || !threshold) {
        return calibration.present() ? "frequency outside the calibrated bands" : "no calibration";
    }
    idle();
    // Front ends off while tuning, as on every band change.
    setPin(m_pins.receiveHighBand, false);
    setPin(m_pins.receiveLowBand, false);
    setPin(m_pins.transmitMidBand, false);
    m_rf.setReferenceTrim(*trim);
    m_rf.setReceiveTune(*tune);
    m_chip.setFrequency(hz);
    m_chip.setBandwidth(channel.wide ? Chip::Bandwidth::Wide : Chip::Bandwidth::Narrow);
    m_wide = channel.wide;
    m_chip.setSquelch(-*threshold, -*threshold - SquelchHysteresisDb);
    const int noiseOpen = squelchOpenNoiseDb(channel.wide, squelchLevel);
    m_chip.setNoiseSquelch(noiseOpen, noiseOpen - SquelchHysteresisDb);
    m_chip.setMode(Chip::Mode::Receive);
    m_receiveHz = hz;
    setPin(frontEnd(), true);
    return nullptr;
}

const char *Rda1846Transceiver::transmit(const FmChannel &channel, PowerLevel level, std::uint16_t ctcssCentiHz,
    const StockCalibration &calibration)
{
    const std::uint32_t hz = channel.transmitHz;
    const auto trim = calibration.referenceTrim();
    const auto power = calibration.txPower(hz, level);
    const auto deviation = calibration.deviation();
    if (!trim || !power || !deviation) {
        return calibration.present() ? "frequency outside the calibrated bands" : "no calibration";
    }
    idle();
    // Band switches before RF: receive front ends off, the transmit filter
    // for the band.
    setPin(m_pins.receiveHighBand, false);
    setPin(m_pins.receiveLowBand, false);
    setPin(m_pins.transmitMidBand, hz >= m_pins.midLowHz && hz <= m_pins.splitHz);
    m_rf.setReferenceTrim(*trim);
    m_rf.setTransmitPath(true);
    setMicrophoneMuted(false);
    m_chip.setFrequency(hz);
    m_chip.setBandwidth(channel.wide ? Chip::Bandwidth::Wide : Chip::Bandwidth::Narrow);
    m_chip.setDeviation(deviation->voice, ctcssCentiHz != 0 ? deviation->ctcss : 0);
    m_chip.setTransmitTone(ctcssCentiHz);
    m_chip.setMode(Chip::Mode::Transmit);
    setPin(m_pins.transmitHighBand, hz > m_pins.splitHz);
    // Power last.
    m_sleepMs(TransmitSettleMs);
    m_rf.setPowerAmplifier(true);
    m_sleepMs(AmplifierSettleMs);
    m_rf.setPowerControl(static_cast<std::uint16_t>(*power / 3));
    m_sleepMs(PowerStepMs);
    m_rf.setPowerControl(static_cast<std::uint16_t>(*power * 2 / 3));
    m_sleepMs(PowerStepMs);
    m_rf.setPowerControl(*power);
    m_power = *power;
    return nullptr;
}

void Rda1846Transceiver::endTransmit()
{
    idle();
    m_chip.setTransmitTone(0);
    setMicrophoneMuted(true);
    setPin(m_pins.transmitHighBand, false);
    setPin(m_pins.transmitMidBand, false);
}

void Rda1846Transceiver::idle()
{
    // The power amplifier goes off before the transceiver stops
    // transmitting.
    m_rf.setPowerAmplifier(false);
    m_rf.setTransmitPath(false);
    m_rf.setPowerControl(0);
    m_power = 0;
    m_chip.setMode(Chip::Mode::Idle);
    m_asleep = false;
}

void Rda1846Transceiver::sleepReceiver()
{
    if (!receiving()) {
        return;
    }
    setPin(frontEnd(), false);
    m_chip.setMode(Chip::Mode::Idle);
    m_asleep = true;
}

void Rda1846Transceiver::wakeReceiver()
{
    if (!m_asleep) {
        return;
    }
    m_chip.setMode(Chip::Mode::Receive);
    setPin(frontEnd(), true);
    m_asleep = false;
}

unsigned Rda1846Transceiver::frontEnd() const
{
    return m_receiveHz > m_pins.splitHz ? m_pins.receiveHighBand : m_pins.receiveLowBand;
}

FmReceiveStatus Rda1846Transceiver::status()
{
    const int quality = m_chip.signalQuality();
    const bool strong = quality < (m_wide ? StrongQualityWide : StrongQualityNarrow);
    const bool flag = (m_chip.flags() & Chip::SquelchOpen) != 0;
    return {flag || strong, m_chip.rssiDbm(), m_chip.noiseDb(), quality};
}

std::uint16_t Rda1846Transceiver::setMicrophoneMuted(bool muted)
{
    return m_chip.setGpio(m_pins.microphoneMute, muted ? Mode::High : Mode::Low);
}

} // namespace dmm::services
