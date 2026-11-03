// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_baseband.h"

#include "dmm/drivers/baseband/tlv320aic3204.h"
#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/os/os.h"

#include <initializer_list>

namespace board {

namespace hal = dmm::hal::gd32f30x;
using hal::PinMode;

namespace {

using CodecWrite = dmm::services::Sct3258Baseband::CodecWrite;
namespace aic = dmm::drivers::baseband::tlv320aic3204;
using aic::hplDb;
using aic::Page0;
using aic::Page1;

constexpr std::uint8_t DacMinus22Db = static_cast<std::uint8_t>(-44);

// The speaker is loud: the DAC is turned down by 22 dB for receive.
constexpr CodecWrite Receive[] = {
    {Page1, aic::LeftMicPositive, 0x00},
    {Page0, aic::LeftDacVolume, DacMinus22Db},
};

// Digital receive: the voice comes from the vocoder at its calibrated
// level, so the DAC stays at 0 dB.
constexpr CodecWrite DigitalReceive[] = {
    {Page1, aic::LeftMicPositive, 0x00},
    {Page0, aic::LeftDacVolume, 0},
};

// Transmit, after the microphone gain: DAC back to 0 dB, the left AGC on
// with its target 10 dB below full scale, then the microphone on the left
// input through 20 kOhm.
constexpr CodecWrite Transmit[] = {
    {Page0, aic::LeftDacVolume, 0},
    {Page0, aic::LeftAgcControl, aic::AgcOn | aic::AgcTargetMinus10Db},
    {Page1, aic::LeftMicPositive, aic::MicIn1Left20k},
};

// The microphone gain is the most the AGC applies, and the amplifier
// volume it starts from. Quiet voices get the whole gain; louder ones are
// held at the target.
dmm::services::Sct3258Baseband::MicrophoneGainWrites microphoneGain(int db)
{
    return {{{Page1, aic::LeftMicPgaVolume, aic::micPgaVolume(db)}, {Page0, aic::LeftAgcMaxGain, aic::agcMaxGain(db)}}};
}

// The demodulated FM signal reaches the baseband with its polarity
// reversed: digital receive decodes nothing otherwise.

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
    .digitalReceive = DigitalReceive,
    .transmit = Transmit,
    .speakerPage = Page1,
    .speakerRegister = aic::HplGain,
    .speakerMuted = aic::HplMuted,
    .speakerSteps = {hplDb(-6), hplDb(-4), hplDb(-1), hplDb(2), hplDb(6), hplDb(8), hplDb(10), hplDb(12), hplDb(13),
        hplDb(15), hplDb(16), hplDb(18), hplDb(20), hplDb(22), hplDb(23)},
    .trims = {{{Page1, aic::LeftOutGain}, {Page1, aic::RightOutGain}, {Page1, aic::RightMicPgaVolume}}},
    .digitalSquelch = DigitalSquelch,
    .analogSquelch = AnalogSquelch,
    .demodulatorInverted = true,
    // Voice enhancement: a little less around 180 Hz, a lift around 2.4 kHz
    // with the rest 1.7 dB down, and a narrow dip at 2 kHz.
    .transmitEqualizer = {{{184.0F, -3.3F, 0.95F, 0.0F}, {2450.0F, 4.1F, 1.25F, -1.7F}, {1990.0F, -3.1F, 4.5F, 0.0F}}},
    .microphoneGain = microphoneGain,
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
