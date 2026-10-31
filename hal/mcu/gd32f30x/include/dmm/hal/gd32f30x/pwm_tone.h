// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/lib/tone_synth.h"

#include <cstdint>

namespace dmm::hal::gd32f30x {

/// A tone on the PWM output of TIMER3 channel 3 (pin PB9 without remap,
/// set to alternate function by the board): 8-bit duty at 120 MHz / 256 =
/// 468.75 kHz, with a new duty from a ToneSynth on every TIMER4 update
/// interrupt at SampleRateHz. The output sits at duty 0 (low) when silent.
class PwmToneOutput
{
public:
    static constexpr std::uint32_t SampleRateHz = 32'000;

    /// Sets both timers up with the sample interrupt at @p irqPriority. The
    /// interrupt must not call the kernel, so any priority will do.
    void start(unsigned irqPriority);

    /// Starts a tone, or changes the running one. @p peak is the highest
    /// duty, 0..255 of the 256-count period.
    void play(std::uint32_t hz, std::uint8_t peak);

    /// Ends the tone at the end of its cycle; the sample interrupt then
    /// stops itself.
    void stop();

    /// Takes each sample's duty (0..255) from @p source, called in the
    /// sample interrupt, until it returns a negative value; replaces a tone.
    void playSamples(int (*source)());

    /// True while a tone or samples play.
    bool playing() const { return m_synth.running() || m_source != nullptr; }

    /// Call from the TIMER4 interrupt handler.
    void onSampleInterrupt();

private:
    lib::ToneSynth m_synth;
    int (*volatile m_source)() = nullptr;
};

} // namespace dmm::hal::gd32f30x
