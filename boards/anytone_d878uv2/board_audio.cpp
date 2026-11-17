// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_audio.h"

namespace board {

namespace hal = dmm::hal::gd32f30x;

hal::PwmToneOutput BoardAudio::s_tone;
bool BoardAudio::s_packet = false;

void BoardAudio::configure()
{
    setToneOutput(false);
    s_tone.start(ToneIrqPriority);
}

void BoardAudio::setAmplifier(bool on)
{
    hal::write(SpeakerAmplifier, on);
}

void BoardAudio::setReceiveRoute(bool on)
{
    hal::write(ReceiveAudioRoute, on);
}

// Floating between beeps, as at power-on. With the pin held low between
// beeps, the muted microphone was heard in the speaker.
void BoardAudio::setToneOutput(bool on)
{
    if (s_packet) {
        return;
    }
    if (on) {
        hal::configure(ToneOutput, hal::PinMode::Alternate, hal::PinSpeed::Medium);
    } else {
        hal::configure(ToneOutput, hal::PinMode::Analog);
    }
}

void BoardAudio::startTone(std::uint16_t hz, int level)
{
    const int clamped = level < 1 ? 1 : level > 15 ? 15
                                                   : level;
    s_tone.play(hz, static_cast<std::uint8_t>(clamped * LoudestTonePeak / 15));
}

void BoardAudio::stopTone()
{
    s_tone.stop();
}

} // namespace board
