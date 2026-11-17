// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/afsk.h"
#include "dmm/services/ax25.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// Made-up received audio for the demodulator tests: 1200 baud AFSK frames
/// with a chosen level for each tone, a sender clock off by a factor, and
/// white noise, at 9600 Hz.
class AfskSignal
{
public:
    static constexpr double RateHz = 9600.0;

    double markAmplitude = 1.0;
    double spaceAmplitude = 1.0;
    /// The sender's tones and bit rate times this.
    double clockScale = 1.0;

    /// Sets the levels for a twist of @p db (space louder than mark for a
    /// positive value) at the same total power.
    void setTwist(double db)
    {
        const double ratio = std::pow(10.0, db / 20.0);
        markAmplitude = std::sqrt(2.0 / (1.0 + ratio * ratio));
        spaceAmplitude = markAmplitude * ratio;
    }

    /// The mean power of the tones while sending.
    double signalPower() const { return (markAmplitude * markAmplitude + spaceAmplitude * spaceAmplitude) / 4.0; }

    /// No tone for @p samples.
    void silence(std::size_t samples) { m_samples.insert(m_samples.end(), samples, 0.0); }

    /// One transmission of @p frame (addresses to check) with flags around it.
    void frame(std::span<const std::uint8_t> frame, std::uint16_t leadFlags, std::uint8_t tailFlags)
    {
        dmm::services::HdlcBits bits;
        bits.start(frame, leadFlags, tailFlags);
        for (int b = bits.next(); b >= 0; b = bits.next()) {
            if (b == 0) {
                m_space = !m_space;
            }
            m_bitPhase -= 1.0;
            while (m_bitPhase < 1.0) {
                const double hz = (m_space ? 2200.0 : 1200.0) * clockScale;
                m_tonePhase += 2.0 * Pi * hz / RateHz;
                if (m_tonePhase > 2.0 * Pi) {
                    m_tonePhase -= 2.0 * Pi;
                }
                m_samples.push_back((m_space ? spaceAmplitude : markAmplitude) * std::sin(m_tonePhase));
                m_bitPhase += 1200.0 * clockScale / RateHz;
            }
        }
    }

    /// Adds noise at @p snrDb below signalPower() to every sample: white,
    /// or rising with frequency like an FM receiver's noise without
    /// de-emphasis (@p fm).
    void addNoise(double snrDb, std::uint32_t seed, bool fm = false)
    {
        m_seed = seed | 1u;
        // The difference of white noise has twice its power.
        const double sigma = std::sqrt(signalPower() / std::pow(10.0, snrDb / 10.0) / (fm ? 2.0 : 1.0));
        double previous = 0.0;
        for (double &s : m_samples) {
            const double n = sigma * gaussian();
            s += fm ? n - previous : n;
            previous = n;
        }
    }

    /// The samples in mV around 1100 mV, @p peakMv per unit of amplitude.
    std::vector<std::uint16_t> millivolts(double peakMv) const
    {
        std::vector<std::uint16_t> out;
        out.reserve(m_samples.size());
        for (double s : m_samples) {
            double v = 1100.0 + s * peakMv + 0.5;
            v = v < 0.0 ? 0.0 : v > 3300.0 ? 3300.0
                                           : v;
            out.push_back(static_cast<std::uint16_t>(v));
        }
        return out;
    }

    const std::vector<double> &samples() const { return m_samples; }

private:
    static constexpr double Pi = 3.14159265358979323846;

    double uniform()
    {
        m_seed ^= m_seed << 13;
        m_seed ^= m_seed >> 17;
        m_seed ^= m_seed << 5;
        return (static_cast<double>(m_seed) + 1.0) / 4294967297.0;
    }

    double gaussian() { return std::sqrt(-2.0 * std::log(uniform())) * std::cos(2.0 * Pi * uniform()); }

    std::vector<double> m_samples;
    double m_tonePhase = 0.0;
    double m_bitPhase = 1.0;
    bool m_space = false;
    std::uint32_t m_seed = 1;
};

/// An APRS-like UI frame from a made-up station, with an info field of
/// @p infoLength characters that differ with @p n.
inline std::vector<std::uint8_t> testFrame(unsigned n, std::size_t infoLength = 60)
{
    namespace ax25 = dmm::services::ax25;
    ax25::Frame f;
    f.source = *ax25::parseAddress("N0CALL-7");
    f.source.ssid = static_cast<std::uint8_t>(n % 16);
    f.destination = *ax25::parseAddress("APZDMM");
    f.path.pushBack(*ax25::parseAddress("WIDE1-1"));
    f.path.pushBack(*ax25::parseAddress("WIDE2-1"));
    std::string info = "!6012.34N/02456.78E>";
    std::uint32_t x = n * 2654435761u + 12345u;
    while (info.size() < infoLength) {
        x = x * 1103515245u + 12345u;
        info.push_back(static_cast<char>(' ' + (x >> 16) % 95));
    }
    f.info << std::string_view(info);
    std::vector<std::uint8_t> bytes(ax25::MaxFrameBytes);
    bytes.resize(ax25::encode(f, bytes));
    return bytes;
}
