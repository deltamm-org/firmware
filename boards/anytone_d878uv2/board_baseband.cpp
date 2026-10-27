// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_baseband.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/os/os.h"

#include <initializer_list>

namespace board {

namespace hal = dmm::hal::gd32f30x;
using hal::PinMode;

namespace {

using CodecWrite = dmm::services::Sct3258Baseband::CodecWrite;

// TLV320AIC3204 registers (page, register).
constexpr std::uint8_t Page0 = 0;
constexpr std::uint8_t Page1 = 1;
constexpr std::uint8_t HplGain = 16; // page 1: headphone-left driver gain, 6-bit dB
constexpr std::uint8_t HplMuted = 0x40;
constexpr std::uint8_t LeftOutGain = 18;       // page 1
constexpr std::uint8_t RightOutGain = 19;      // page 1
constexpr std::uint8_t LeftMicPositive = 52;   // page 1: 0 = nothing connected
constexpr std::uint8_t LeftMicPgaVolume = 59;  // page 1
constexpr std::uint8_t RightMicPgaVolume = 60; // page 1
constexpr std::uint8_t LeftDacVolume = 65;     // page 0: signed, 0.5 dB steps
constexpr std::uint8_t LeftAgcControl = 86;    // page 0
constexpr std::uint8_t LeftAgcMaxGain = 88;    // page 0

constexpr std::uint8_t MicIn1Left20k = 0x80;
constexpr std::uint8_t MicGain16Db = 32;
constexpr std::uint8_t AgcOn = 0x80;
constexpr std::uint8_t DacMinus22Db = static_cast<std::uint8_t>(-44);

constexpr std::uint8_t hplDb(int db)
{
    return static_cast<std::uint8_t>(db & 0x3F);
}

// The speaker is loud: the DAC is turned down by 22 dB for receive.
constexpr CodecWrite Receive[] = {
    {Page1, LeftMicPositive, 0x00},
    {Page0, LeftDacVolume, DacMinus22Db},
};

// Transmit: DAC back to 0 dB, the microphone on the left input through
// 20 kOhm, input amplifier +16 dB, left AGC on with the same target; the
// microphone is connected last.
constexpr CodecWrite Transmit[] = {
    {Page0, LeftDacVolume, 0},
    {Page1, LeftMicPgaVolume, MicGain16Db},
    {Page0, LeftAgcControl, AgcOn | MicGain16Db},
    {Page0, LeftAgcMaxGain, MicGain16Db},
    {Page1, LeftMicPositive, MicIn1Left20k},
};

// The baseband's own carrier squelch: no gain adjust, no forced RF gain,
// a check every 80 ms (60 units), noise thresholds 0x0F3C high and 0x0A8C
// low. On analog channels the analog thresholds are off.
constexpr std::uint8_t DigitalSquelch[] = {0x00, 0x00, 0x00, 0x3C, 0x0F, 0x3C, 0x0A, 0x8C, 0x0F, 0x3C, 0x0A, 0x8C};
constexpr std::uint8_t AnalogSquelch[] = {0x00, 0x00, 0x00, 0x3C, 0x0F, 0x3C, 0x0A, 0x8C, 0xFF, 0xFF, 0xFF, 0xFF};

// The driver gain spans -6 to +23 dB over the volume steps, more finely at
// the bottom.
constexpr dmm::services::Sct3258Baseband::Audio Audio{
    .codec = dmm::drivers::baseband::Sct3258::CodecTlv320aic3204,
    .receive = Receive,
    .transmit = Transmit,
    .speakerPage = Page1,
    .speakerRegister = HplGain,
    .speakerMuted = HplMuted,
    .speakerSteps = {hplDb(-6), hplDb(-4), hplDb(-1), hplDb(2), hplDb(6), hplDb(8), hplDb(10), hplDb(12), hplDb(13),
        hplDb(15), hplDb(16), hplDb(18), hplDb(20), hplDb(22), hplDb(23)},
    .trims = {{{Page1, LeftOutGain}, {Page1, RightOutGain}, {Page1, RightMicPgaVolume}}},
    .digitalSquelch = DigitalSquelch,
    .analogSquelch = AnalogSquelch,
};

void outputLow(Pin pin)
{
    hal::write(pin, false);
    hal::configure(pin, PinMode::Output);
}

} // namespace

void BoardBaseband::configurePins()
{
    m_port.release();
    for (Pin pin : {BasebandEnableFirst, BasebandEnableSecond, BasebandReset, BasebandWake, BasebandRestart, BasebandSleep,
             BasebandModeStrap, BasebandPort.chipSelect, BasebandPort.write, BasebandPort.read}) {
        outputLow(pin);
    }
}

void BoardBaseband::powerUpAndReset()
{
    // From any state, also a running chip: reset, power off and let it
    // drain, with every line low. The mode strap must be driven low again:
    // after start-up the chip uses that line itself.
    configurePins();
    dmm::os::sleepMs(BasebandOffMs);

    hal::write(BasebandEnableFirst, true);
    m_port.configurePins(); // strobes and select idle high, data lines in
    hal::write(BasebandEnableSecond, true);
    dmm::os::sleepMs(BasebandSettleMs);
    hal::write(BasebandReset, true);
    dmm::os::sleepMs(BasebandResetHighMs);
    hal::write(BasebandReset, false);
    dmm::os::sleepMs(BasebandResetLowMs);
    hal::write(BasebandReset, true); // the chip now waits for its boot program
    hal::configure(BasebandModeStrap, PinMode::InputPullDown);
}

void BoardBaseband::pollDelayUs(unsigned us)
{
    static unsigned busyUs = 0;
    busyUs += us;
    if (busyUs >= 1000) {
        busyUs = 0;
        dmm::os::sleepMs(1);
        return;
    }
    dmm::hal::cortex_m::busyWaitCycles(us * (hal::SystemClockHz / 1'000'000));
}

const dmm::services::Sct3258Baseband::Audio &basebandAudio()
{
    return Audio;
}

} // namespace board
