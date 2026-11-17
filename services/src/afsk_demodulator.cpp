// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/afsk_demodulator.h"

#include <algorithm>
#include <cmath>

namespace dmm::services {

namespace {

constexpr double Pi = 3.14159265358979323846;
// The coefficients are worked out in float, as they are kept.
constexpr float TwoPi = 2.0F * static_cast<float>(Pi);
constexpr float Rate = AfskDemodulator::SampleRateHz;
constexpr double MarkHz = 1200.0;
constexpr double SpaceHz = 2200.0;
// The band-pass filter's edges; the tones and their keying sidebands lie
// between.
constexpr double LowEdgeHz = 600.0;
constexpr double HighEdgeHz = 2600.0;
// The bit clock advances this much per sample, in bits.
constexpr float ClockStep = 1200.0F / static_cast<float>(AfskDemodulator::SampleRateHz);
// How far the bit clock moves towards a tone change: further while it
// looks for frames, less inside one.
constexpr float SearchPull = 0.5F;
constexpr float FramePull = 0.1F;
// How fast the bit clock's rate follows the sender's inside a frame, and
// how far it may go (3 %).
constexpr float DriftPull = 0.0005F;
constexpr float MaxDrift = 0.03F * ClockStep;
// The DC level follows the input this fast (per sample).
constexpr float DcFollow = 1.0F / 1024.0F;
// Copies of a frame end within this many samples of each other.
constexpr std::uint32_t CopyWindow = AfskDemodulator::SampleRateHz;
// A repaired frame waits this long (three bits) for a slicer that reads it
// without a repair.
constexpr std::uint32_t RepairWait = 3 * AfskDemodulator::SampleRateHz / 1200;
constexpr std::uint16_t CheckPolynomial = 0x8408;
// The check register after a frame and its right check.
constexpr std::uint16_t GoodResidue = 0xF0B8;
constexpr std::uint8_t UiControl = 0x03;
constexpr std::uint8_t NoLayer3 = 0xF0;
constexpr std::size_t MaxAddresses = 2 + ax25::MaxPath;

std::uint16_t checkStep(std::uint16_t crc)
{
    return (crc & 1) != 0 ? static_cast<std::uint16_t>((crc >> 1) ^ CheckPolynomial) : static_cast<std::uint16_t>(crc >> 1);
}

// The check register over @p bytes, from the start value, not inverted.
std::uint16_t checkRegister(std::span<const std::uint8_t> bytes)
{
    std::uint16_t crc = 0xFFFF;
    for (std::uint8_t b : bytes) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = checkStep(crc);
        }
    }
    return crc;
}

bool isAddressChar(std::uint8_t b)
{
    const char c = static_cast<char>(b >> 1);
    return (b & 1) == 0 && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ');
}

// The frame starts with 2 to 10 addresses of letters, digits and spaces,
// then the UI control byte and no layer 3.
bool wellFormedUi(std::span<const std::uint8_t> frame)
{
    std::size_t at = 0;
    for (std::size_t a = 0; a < MaxAddresses; ++a) {
        if (at + 7 + 2 + 2 > frame.size()) {
            return false;
        }
        for (std::size_t i = 0; i < 6; ++i) {
            if (!isAddressChar(frame[at + i])) {
                return false;
            }
        }
        const bool last = (frame[at + 6] & 1) != 0;
        at += 7;
        if (last) {
            return a >= 1 && frame[at] == UiControl && frame[at + 1] == NoLayer3;
        }
    }
    return false;
}

void flipBit(std::span<std::uint8_t> frame, std::size_t bit)
{
    frame[bit / 8] = static_cast<std::uint8_t>(frame[bit / 8] ^ (1u << (bit % 8)));
}

} // namespace

AfskDemodulator::AfskDemodulator()
{
    // A windowed-sinc band-pass filter with no response at DC.
    constexpr float Mid = (FilterTaps - 1) / 2.0F;
    float sum = 0.0F;
    for (std::size_t k = 0; k < FilterTaps; ++k) {
        const float t = static_cast<float>(k) - Mid;
        const auto lowPass = [t](double edgeHz) {
            const float f = static_cast<float>(edgeHz) / Rate;
            return t == 0.0F ? 2.0F * f : std::sin(TwoPi * f * t) / (TwoPi / 2.0F * t);
        };
        const float window = 0.54F - 0.46F * std::cos(TwoPi * static_cast<float>(k) / (FilterTaps - 1));
        m_filter[k] = (lowPass(HighEdgeHz) - lowPass(LowEdgeHz)) * window;
        sum += m_filter[k];
    }
    for (float &tap : m_filter) {
        tap -= sum / FilterTaps;
    }

    // Correlators over one bit, scaled so that either tone at amplitude 1
    // gives about 1.
    constexpr float Scale = 2.0F / BitSamples;
    const float markStep = TwoPi * static_cast<float>(MarkHz) / Rate;
    const float spaceStep = TwoPi * static_cast<float>(SpaceHz) / Rate;
    for (std::size_t k = 0; k < BitSamples; ++k) {
        const auto n = static_cast<float>(k);
        m_markCos[k] = Scale * std::cos(markStep * n);
        m_markSin[k] = Scale * std::sin(markStep * n);
        m_spaceCos[k] = Scale * std::cos(spaceStep * n);
        m_spaceSin[k] = Scale * std::sin(spaceStep * n);
    }

    // Each slicer's tilt: a three-tap filter (s, a, s) on the correlator
    // outputs of the last three samples, which equals that filter on the
    // audio before the correlators. Its gain is a + 2 s cos(w) at a tone of
    // w radians per sample; a is chosen for the slicer's gain ratio.
    const float markCos = std::cos(markStep);
    const float spaceCos = std::cos(spaceStep);
    for (std::size_t i = 0; i < Slicers; ++i) {
        Slicer &s = m_slicers[i];
        if (MarkGainDb[i] == 0) {
            s.tilt = {0.0F, 1.0F};
            continue;
        }
        const float ratio = std::pow(10.0F, static_cast<float>(MarkGainDb[i]) / 20.0F);
        const float side = MarkGainDb[i] > 0 ? 1.0F : -1.0F;
        s.tilt = {side, 2.0F * side * (markCos - ratio * spaceCos) / (ratio - 1.0F)};
    }
}

void AfskDemodulator::reset()
{
    m_input.fill(0.0F);
    m_filtered.fill(0.0F);
    m_dcKnown = false;
    m_mark.fill({});
    m_space.fill({});
    m_heldLength = 0;
    for (Slicer &s : m_slicers) {
        const std::array<float, 2> tilt = s.tilt;
        s = Slicer{};
        s.tilt = tilt;
    }
}

void AfskDemodulator::process(std::span<const std::uint16_t> mv, Sink onFrame)
{
    for (std::uint16_t v : mv) {
        sample(static_cast<float>(v), onFrame);
    }
}

void AfskDemodulator::process(std::span<const float> samples, Sink onFrame)
{
    for (float v : samples) {
        sample(v, onFrame);
    }
}

void AfskDemodulator::sample(float x, Sink &onFrame)
{
    ++m_stats.samples;
    if (!m_dcKnown) {
        m_dc = x;
        m_dcKnown = true;
    }
    m_dc += (x - m_dc) * DcFollow;
    x -= m_dc;

    m_inputAt = m_inputAt == 0 ? FilterTaps - 1 : m_inputAt - 1;
    m_input[m_inputAt] = x;
    m_input[m_inputAt + FilterTaps] = x;
    // Newest first from m_inputAt; the filter is symmetric.
    const float *in = &m_input[m_inputAt];
    float y = 0.0F;
    for (std::size_t k = 0; k < FilterTaps; ++k) {
        y += m_filter[k] * in[k];
    }

    m_filteredAt = m_filteredAt == 0 ? BitSamples - 1 : m_filteredAt - 1;
    m_filtered[m_filteredAt] = y;
    m_filtered[m_filteredAt + BitSamples] = y;
    const float *f = &m_filtered[m_filteredAt];
    float mc = 0.0F;
    float ms = 0.0F;
    float sc = 0.0F;
    float ss = 0.0F;
    for (std::size_t k = 0; k < BitSamples; ++k) {
        mc += m_markCos[k] * f[k];
        ms += m_markSin[k] * f[k];
        sc += m_spaceCos[k] * f[k];
        ss += m_spaceSin[k] * f[k];
    }
    m_mark[2] = m_mark[1];
    m_mark[1] = m_mark[0];
    m_mark[0] = {mc, ms};
    m_space[2] = m_space[1];
    m_space[1] = m_space[0];
    m_space[0] = {sc, ss};
    for (std::size_t i = 0; i < Slicers; ++i) {
        const float side = m_slicers[i].tilt[0];
        const float middle = m_slicers[i].tilt[1];
        const float mi = side * (m_mark[0].i + m_mark[2].i) + middle * m_mark[1].i;
        const float mq = side * (m_mark[0].q + m_mark[2].q) + middle * m_mark[1].q;
        const float si = side * (m_space[0].i + m_space[2].i) + middle * m_space[1].i;
        const float sq = side * (m_space[0].q + m_space[2].q) + middle * m_space[1].q;
        // Mark power minus space power: above 0 for mark.
        slice(i, (mi * mi + mq * mq) - (si * si + sq * sq), onFrame);
    }
    if (m_heldLength != 0 && m_stats.samples - m_heldAt >= RepairWait) {
        passHeld(onFrame);
    }
}

void AfskDemodulator::slice(std::size_t index, float decision, Sink &onFrame)
{
    Slicer &s = m_slicers[index];
    s.phase += ClockStep + s.drift;
    if ((decision > 0.0F) != (s.last > 0.0F)) {
        // The tone changed between the last sample and this one, where the
        // decision crossed zero; that should be halfway between bits.
        const float fraction = s.last / (s.last - decision);
        float error = s.phase - (1.0F - fraction) * ClockStep - 0.5F;
        if (error > 0.5F) {
            error -= 1.0F;
        } else if (error < -0.5F) {
            error += 1.0F;
        }
        s.phase -= error * (s.inFrame ? FramePull : SearchPull);
        if (s.inFrame) {
            // The sender's bit rate differs from ours: follow it slowly.
            s.drift -= error * DriftPull;
            s.drift = s.drift > MaxDrift ? MaxDrift : s.drift < -MaxDrift ? -MaxDrift
                                                                          : s.drift;
        } else {
            s.drift = 0.0F;
        }
    }
    if (s.phase >= 1.0F) {
        // The middle of a bit lies between the last sample and this one.
        float fraction = (1.0F - (s.phase - ClockStep)) / ClockStep;
        fraction = fraction < 0.0F ? 0.0F : fraction > 1.0F ? 1.0F
                                                            : fraction;
        const bool tone = s.last + (decision - s.last) * fraction > 0.0F;
        s.phase -= 1.0F;
        // NRZI: the same tone is a 1, a change a 0.
        bit(index, tone == s.lastTone, onFrame);
        s.lastTone = tone;
    }
    s.last = decision;
}

void AfskDemodulator::bit(std::size_t index, bool one, Sink &onFrame)
{
    Slicer &s = m_slicers[index];
    if (one) {
        if (s.ones < 7) {
            ++s.ones;
        }
        if (s.ones == 7) {
            // An abort, or no signal.
            s.inFrame = false;
            return;
        }
        if (s.ones == 6) {
            // A flag or an abort follows.
            return;
        }
    } else {
        const std::uint8_t ones = s.ones;
        s.ones = 0;
        if (ones == 6) {
            flag(index, onFrame);
            return;
        }
        if (ones == 5) {
            // Stuffed.
            return;
        }
    }
    if (!s.inFrame) {
        return;
    }
    s.byte = static_cast<std::uint8_t>((s.byte >> 1) | (one ? 0x80 : 0));
    ++s.bits;
    if (s.bits % 8 == 0) {
        const std::size_t at = s.bits / 8 - 1;
        if (at >= MaxFrame) {
            s.inFrame = false;
            return;
        }
        s.bytes[at] = s.byte;
    }
}

void AfskDemodulator::flag(std::size_t index, Sink &onFrame)
{
    Slicer &s = m_slicers[index];
    // The flag's first six bits (a 0 and five 1s) went in as data.
    if (s.inFrame && s.bits >= 6 && (s.bits - 6) % 8 == 0) {
        const std::size_t length = (s.bits - 6) / 8;
        if (length >= MinFrame) {
            const std::span<std::uint8_t> frame(s.bytes.data(), length);
            if (checkRegister(frame) == GoodResidue) {
                pass(index, frame, false, onFrame);
            } else {
                ++m_stats.failed[index];
                if (m_repair && tryRepair(frame)) {
                    hold(index, frame, onFrame);
                }
            }
        }
    }
    s.inFrame = true;
    s.bits = 0;
}

void AfskDemodulator::pass(std::size_t index, std::span<const std::uint8_t> frame, bool repaired, Sink &onFrame)
{
    if (!repaired) {
        ++m_stats.frames[index];
        m_cleanAt = m_stats.samples;
        m_cleanSeen = true;
    }
    if (m_recent.seen(frame, m_stats.samples, CopyWindow)) {
        ++m_stats.duplicates;
        return;
    }
    ++m_stats.passed;
    if (repaired) {
        ++m_stats.repaired;
    }
    onFrame(frame, FrameInfo{static_cast<std::uint8_t>(index), repaired});
}

void AfskDemodulator::hold(std::size_t index, std::span<const std::uint8_t> frame, Sink &onFrame)
{
    // A wrong repair would match the right frame's check too, so it must
    // not get ahead of a slicer that reads the frame as it is.
    if (m_heldLength != 0) {
        passHeld(onFrame);
    }
    std::copy(frame.begin(), frame.end(), m_held.begin());
    m_heldLength = frame.size();
    m_heldSlicer = static_cast<std::uint8_t>(index);
    m_heldAt = m_stats.samples;
}

void AfskDemodulator::passHeld(Sink &onFrame)
{
    const std::size_t length = m_heldLength;
    m_heldLength = 0;
    // A frame read without a repair ended close by: the repaired one is
    // most likely a damaged copy of it, perhaps with a damaged check, a
    // byte of junk before it or after it, so it would not be seen as one.
    const auto apart = static_cast<std::int32_t>(m_heldAt - m_cleanAt);
    const std::uint32_t distance = static_cast<std::uint32_t>(apart < 0 ? -apart : apart);
    if (m_cleanSeen && distance < CopyWindow) {
        ++m_stats.duplicates;
        return;
    }
    pass(m_heldSlicer, std::span<const std::uint8_t>(m_held.data(), length), true, onFrame);
}

bool AfskDemodulator::tryRepair(std::span<std::uint8_t> frame) const
{
    // The check is linear: a flipped bit changes the final register by a
    // value that depends only on how many bits follow it. Walking from the
    // last bit back, that value is found for each bit in turn, and for
    // each pair of neighbouring bits (one wrong tone changes two bits).
    const std::uint16_t syndrome = checkRegister(frame) ^ GoodResidue;
    const std::size_t bits = frame.size() * 8;
    std::uint16_t after = 0;
    std::uint16_t effect = CheckPolynomial;
    for (std::size_t i = bits; i-- > 0;) {
        if (effect == syndrome) {
            flipBit(frame, i);
            if (wellFormedUi(frame)) {
                return true;
            }
            flipBit(frame, i);
        }
        if (i + 1 < bits && (effect ^ after) == syndrome) {
            flipBit(frame, i);
            flipBit(frame, i + 1);
            if (wellFormedUi(frame)) {
                return true;
            }
            flipBit(frame, i);
            flipBit(frame, i + 1);
        }
        after = effect;
        effect = checkStep(effect);
    }
    return false;
}

} // namespace dmm::services
