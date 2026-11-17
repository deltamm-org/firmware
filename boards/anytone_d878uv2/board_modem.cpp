// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/board/board_modem.h"

#include "board_audio.h"
#include "pins.h"

#include "dmm/services/afsk.h"

namespace board {

namespace {

namespace hal = dmm::hal::gd32f30x;
using dmm::services::AfskModulator;

AfskModulator modulator;

int nextSample()
{
    return modulator.running() ? modulator.next() : -1;
}

} // namespace

bool BoardModem::send(std::span<const std::uint8_t> frame, std::uint16_t leadMs, std::uint8_t levelPercent)
{
    if (BoardAudio::s_packet) {
        return false;
    }
    // A flag is eight bits.
    const auto leadFlags = static_cast<std::uint16_t>((leadMs * AfskModulator::BaudRate / 8 + 999) / 1000);
    const unsigned level = levelPercent > 100 ? 100 : levelPercent;
    modulator.start(frame, leadFlags == 0 ? 1 : leadFlags, PacketTailFlags, hal::PwmToneOutput::SampleRateHz,
        static_cast<std::uint8_t>(level * 255 / 100));
    BoardAudio::s_packet = true;
    hal::configure(ToneOutput, hal::PinMode::Alternate, hal::PinSpeed::Medium);
    BoardAudio::s_tone.playSamples(&nextSample);
    return true;
}

bool BoardModem::sending()
{
    if (BoardAudio::s_packet && !BoardAudio::s_tone.playing()) {
        BoardAudio::s_packet = false;
        hal::configure(ToneOutput, hal::PinMode::Analog);
    }
    return BoardAudio::s_packet;
}

} // namespace board
