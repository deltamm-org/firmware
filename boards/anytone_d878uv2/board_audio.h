// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "pins.h"

#include "dmm/hal/gd32f30x/pwm_tone.h"
#include "dmm/services/audio.h"

namespace board {

/// The speaker amplifier, the switch that feeds it the codec's receive
/// audio, and beep tones on a PWM pin mixed in before the amplifier.
class BoardAudio final : public dmm::services::AudioHardware
{
public:
    /// Pins and timers; the amplifier stays off.
    void configure();

    void setAmplifier(bool on) override;
    void setReceiveRoute(bool on) override;
    void startTone(std::uint16_t hz, int level) override;
    void stopTone() override;
    void setToneOutput(bool on) override;
    unsigned amplifierSettleMs() const override { return AmplifierSettleMs; }

    /// Call from the tone sample interrupt.
    static void onToneInterrupt() { s_tone.onSampleInterrupt(); }

private:
    friend class BoardModem;

    static dmm::hal::gd32f30x::PwmToneOutput s_tone;
    // A packet holds the tone pin until its last sample.
    static bool s_packet;
};

} // namespace board
