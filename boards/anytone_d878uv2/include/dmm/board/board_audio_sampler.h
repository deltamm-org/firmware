// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/audio_sampler.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace board {

/// The received audio on its ADC input, sampled at a fixed rate into a
/// ring in the background (services::AudioSampler).
class BoardAudioSampler
{
public:
    /// Samples in the ring: about 0.4 s at 9600 Hz, the longest a reader
    /// may wait between reads.
    static constexpr std::size_t RingSamples = 4096;

    bool start(std::uint32_t rateHz);
    std::size_t read(std::span<std::uint16_t> out);
    std::uint32_t lostSamples() const { return m_lost; }
    /// Stops, and logs the extremes in ADC counts.
    void stop();

private:
    std::uint32_t written();

    std::uint32_t m_rateHz = 0;
    std::uint32_t m_startMs = 0;
    std::uint32_t m_written = 0;
    std::uint32_t m_read = 0;
    std::uint32_t m_lost = 0;
    std::uint16_t m_rawMin = 0xFFFF;
    std::uint16_t m_rawMax = 0;
    bool m_running = false;
};

} // namespace board
