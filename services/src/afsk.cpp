// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/afsk.h"

namespace dmm::services {

namespace {

std::uint32_t phaseStep(std::uint32_t hz, std::uint32_t sampleRateHz)
{
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(hz) << 32) / (sampleRateHz == 0 ? 1 : sampleRateHz));
}

} // namespace

void HdlcBits::start(std::span<const std::uint8_t> frame, std::uint16_t leadFlags, std::uint8_t tailFlags)
{
    m_frame = frame;
    m_index = 0;
    m_flagsLeft = leadFlags;
    m_tailFlags = tailFlags;
    m_bit = 8;
    m_ones = 0;
    m_stuff = false;
    m_part = Part::Lead;
}

int HdlcBits::next()
{
    if (m_stuff) {
        m_stuff = false;
        m_ones = 0;
        return 0;
    }
    while (m_bit == 8) {
        switch (m_part) {
        case Part::Lead:
            if (m_flagsLeft == 0) {
                m_part = Part::Frame;
                continue;
            }
            --m_flagsLeft;
            m_byte = Flag;
            break;
        case Part::Frame:
            if (m_index == m_frame.size()) {
                m_part = Part::Tail;
                m_flagsLeft = m_tailFlags;
                continue;
            }
            m_byte = m_frame[m_index++];
            break;
        case Part::Tail:
            if (m_flagsLeft == 0) {
                m_part = Part::Done;
                continue;
            }
            --m_flagsLeft;
            m_byte = Flag;
            break;
        case Part::Done:
            return -1;
        }
        m_bit = 0;
    }
    const int bit = (m_byte >> m_bit) & 1;
    ++m_bit;
    if (m_part == Part::Frame && bit == 1) {
        if (++m_ones == 5) {
            m_stuff = true;
        }
    } else {
        m_ones = 0;
    }
    return bit;
}

void AfskModulator::start(std::span<const std::uint8_t> frame, std::uint16_t leadFlags, std::uint8_t tailFlags,
    std::uint32_t sampleRateHz, std::uint8_t peak)
{
    m_running = false;
    m_bits.start(frame, leadFlags, tailFlags);
    m_markStep = phaseStep(MarkHz, sampleRateHz);
    m_spaceStep = phaseStep(SpaceHz, sampleRateHz);
    m_bitStep = phaseStep(BaudRate, sampleRateHz);
    m_bitPhase = 0;
    m_phase = 0;
    m_peak = peak;
    m_space = false;
    nextBit();
    m_running = true;
}

void AfskModulator::nextBit()
{
    const int bit = m_bits.next();
    if (bit < 0) {
        m_running = false;
    } else if (bit == 0) {
        m_space = !m_space;
    }
}

std::uint8_t AfskModulator::next()
{
    if (!m_running) {
        return 0;
    }
    m_phase += m_space ? m_spaceStep : m_markStep;
    const auto level = static_cast<std::uint8_t>((lib::ToneSynth::Wave[m_phase >> 26] * m_peak) >> 8);
    const std::uint32_t previous = m_bitPhase;
    m_bitPhase = previous + m_bitStep;
    if (m_bitPhase < previous) {
        nextBit();
    }
    return level;
}

} // namespace dmm::services
