// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/rda1846_transceiver.h"

#include "dmm/lib/format.h"
#include "dmm/lib/strings.h"
#include "dmm/services/log.h"

namespace dmm::services {

namespace {

using Chip = drivers::radio::Rda1846;
namespace log = services::log;
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

Rda1846Transceiver::Rda1846Transceiver(Chip &chip, RfHardware &rf, const Pins &pins, void (*sleepMs)(unsigned),
    TuningSource tuning)
    : m_chip(chip), m_rf(rf), m_pins(pins), m_sleepMs(sleepMs), m_tuning(tuning)
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
    if (m_tuning != nullptr) {
        m_chip.setTuning(m_tuning());
    }
    if (!m_chip.start(m_sleepMs, startGpio())) {
        log::error("fm") << "RDA1846 does not answer (id register " << lib::hex(m_chip.readRegister(0), 4) << ")";
        return false;
    }
    return true;
}

const char *Rda1846Transceiver::receive(const FmChannel &channel, int squelchLevel, const Calibration &calibration)
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
    m_tune = *tune;
    m_chip.setFrequency(hz);
    m_chip.setBandwidth(channel.wide ? Chip::Bandwidth::Wide : Chip::Bandwidth::Narrow);
    m_wide = channel.wide;
    m_chip.setSquelch(-*threshold, -*threshold - SquelchHysteresisDb);
    const int noiseOpen = squelchOpenNoiseDb(channel.wide, squelchLevel);
    m_chip.setNoiseSquelch(noiseOpen, noiseOpen - SquelchHysteresisDb);
    log::info("fm") << "noise squelch opens below " << noiseOpen << " dB";
    m_chip.setMode(Chip::Mode::Receive);
    m_receiveHz = hz;
    setPin(frontEnd(), true);
    return nullptr;
}

const char *Rda1846Transceiver::transmit(const FmChannel &channel, PowerLevel level, std::uint16_t ctcssCentiHz,
    const Calibration &calibration)
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
    log::info("fm") << "power control " << m_power;
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
    return {flag || strong, m_chip.rssiDbm()};
}

void Rda1846Transceiver::logStatus(std::string_view what)
{
    log::info("fm") << what << ", RSSI " << m_chip.rssiDbm() << " dBm, noise " << m_chip.noiseDb() << " dB, quality "
                    << m_chip.signalQuality();
}

void Rda1846Transceiver::setMicrophoneMuted(bool muted)
{
    m_chip.setGpio(m_pins.microphoneMute, muted ? Mode::High : Mode::Low);
}

// Mean signal strength over a few reads, after the receiver has settled.
int Rda1846Transceiver::settledRssi()
{
    constexpr int Reads = 4;
    m_sleepMs(25);
    int sum = 0;
    for (int i = 0; i < Reads; ++i) {
        sum += m_chip.rssiDbm();
        m_sleepMs(5);
    }
    return sum / Reads;
}

// Front-end tuning against signal strength: a coarse pass over the whole
// range, then a fine pass around the strongest point; then the calibrated
// value goes back.
void Rda1846Transceiver::sweep()
{
    const int max = m_pins.receiveTuneMax;
    const int coarse = (max + 1) / 32 > 0 ? (max + 1) / 32 : 1;
    const int fine = coarse / 8 > 0 ? coarse / 8 : 1;
    log::info("fm") << "sweep, calibrated tune " << m_tune;
    int best = 0;
    int bestRssi = -200;
    auto measure = [&](int v) {
        m_rf.setReceiveTune(static_cast<std::uint16_t>(v));
        const int rssi = settledRssi();
        log::info("fm") << "tune " << v << ": " << rssi << " dBm";
        if (rssi > bestRssi) {
            bestRssi = rssi;
            best = v;
        }
    };
    for (int v = 0; v <= max; v += coarse) {
        measure(v);
    }
    measure(max);
    const int from = best - 7 * fine < 0 ? 0 : best - 7 * fine;
    const int to = best + 7 * fine > max ? max : best + 7 * fine;
    for (int v = from; v <= to; v += fine) {
        measure(v);
    }
    m_rf.setReceiveTune(m_tune);
    log::info("fm") << "sweep: strongest " << bestRssi << " dBm at tune " << best << "; calibrated " << m_tune << " gives "
                    << settledRssi() << " dBm (put back)";
}

bool Rda1846Transceiver::debugCommand(std::string_view command)
{
    if (command == "sweep" || lib::startsWith(command, "tune ")) {
        if (!receiving()) {
            log::info("fm") << "not receiving";
            return true;
        }
        if (command == "sweep") {
            sweep();
            return true;
        }
        std::string_view rest = command;
        lib::takeWord(rest);
        const bool calibrated = rest == "auto";
        const auto value = calibrated ? std::optional<std::int32_t>(m_tune) : lib::parseNumber(rest, 10);
        if (!value || *value > m_pins.receiveTuneMax) {
            return false;
        }
        m_rf.setReceiveTune(static_cast<std::uint16_t>(*value));
        log::info("fm") << "tune " << *value << (calibrated ? " (calibrated)" : " (held)") << ": " << settledRssi() << " dBm";
        return true;
    }
    std::string_view rest = command;
    const auto reg = lib::parseNumber(lib::takeWord(rest), 16);
    if (!reg || *reg > 0x7F) {
        return false;
    }
    const auto address = static_cast<std::uint8_t>(*reg);
    if (rest.empty()) {
        log::info("fm") << "register " << lib::hex(address, 2) << " = " << lib::hex(m_chip.readRegister(address), 4);
        return true;
    }
    const auto value = lib::parseNumber(lib::takeWord(rest), 16);
    if (!value || *value > 0xFFFF || !rest.empty()) {
        return false;
    }
    m_chip.writeRegister(address, static_cast<std::uint16_t>(*value));
    log::info("fm") << "register " << lib::hex(address, 2) << " = " << lib::hex(*value, 4) << ", reads back "
                    << lib::hex(m_chip.readRegister(address), 4);
    return true;
}

const char *Rda1846Transceiver::debugHelp() const
{
    return "fm <reg> [value] (hex), fm tune <value>|auto, fm sweep";
}

} // namespace dmm::services
