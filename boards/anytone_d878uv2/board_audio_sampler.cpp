// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/board/board_audio_sampler.h"

#include "pins.h"

#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/timed_adc.h"
#include "dmm/os/os.h"
#include "dmm/services/log.h"

namespace board {

namespace {

namespace hal = dmm::hal::gd32f30x;

hal::TimedAdc timedAdc;
std::uint16_t ring[BoardAudioSampler::RingSamples];

// The oldest samples, which the DMA overwrites next, are given up this
// early, so none is overwritten while it is copied.
constexpr std::uint32_t Margin = 64;

} // namespace

bool BoardAudioSampler::start(std::uint32_t rateHz)
{
    stop();
    hal::configure(ReceiveAudioSense, hal::PinMode::Analog);
    if (!timedAdc.start(ReceiveAudioChannel, rateHz, ring)) {
        return false;
    }
    m_startMs = dmm::os::nowMs();
    m_rateHz = rateHz;
    m_written = 0;
    m_read = 0;
    m_lost = 0;
    m_rawMin = 0xFFFF;
    m_rawMax = 0;
    m_running = true;
    return true;
}

// Samples written since start: the ring position gives the count modulo
// the ring, the time since start the rest.
std::uint32_t BoardAudioSampler::written()
{
    const std::uint32_t elapsedMs = dmm::os::nowMs() - m_startMs;
    const std::uint32_t estimate = static_cast<std::uint32_t>(static_cast<std::uint64_t>(elapsedMs) * m_rateHz / 1000);
    const std::uint32_t position = static_cast<std::uint32_t>(timedAdc.position());
    constexpr std::uint32_t Size = RingSamples;
    std::uint32_t total = estimate - estimate % Size + position;
    if (total > estimate + Size / 2) {
        total = total >= Size ? total - Size : 0;
    } else if (total + Size / 2 < estimate) {
        total += Size;
    }
    if (total > m_written) {
        m_written = total;
    }
    return m_written;
}

std::size_t BoardAudioSampler::read(std::span<std::uint16_t> out)
{
    if (!m_running) {
        return 0;
    }
    const std::uint32_t total = written();
    std::uint32_t available = total - m_read;
    if (available > RingSamples - Margin) {
        const std::uint32_t skipped = available - (RingSamples - Margin);
        m_lost += skipped;
        m_read += skipped;
        available -= skipped;
    }
    const std::size_t n = available < out.size() ? available : out.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint16_t raw = ring[(m_read + i) % RingSamples] & 0xFFFu;
        m_rawMin = raw < m_rawMin ? raw : m_rawMin;
        m_rawMax = raw > m_rawMax ? raw : m_rawMax;
        out[i] = static_cast<std::uint16_t>((raw * AdcFullScaleMv + 2047) / 4095);
    }
    m_read += static_cast<std::uint32_t>(n);
    return n;
}

void BoardAudioSampler::stop()
{
    if (!m_running) {
        return;
    }
    timedAdc.stop();
    m_running = false;
    if (m_read > 0) {
        dmm::services::log::info("audio") << "ADC channel " << ReceiveAudioChannel << ": " << m_read << " samples, counts "
                                          << m_rawMin << ".." << m_rawMax << " of 4095";
    }
}

} // namespace board
