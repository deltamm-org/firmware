// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/afsk.h"
#include "dmm/services/ax25.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace dmm::services;

namespace {

std::vector<int> allBits(HdlcBits &h)
{
    std::vector<int> bits;
    for (int b = h.next(); b >= 0; b = h.next()) {
        bits.push_back(b);
    }
    return bits;
}

std::vector<int> byteBits(std::uint8_t b)
{
    std::vector<int> bits;
    for (int i = 0; i < 8; ++i) {
        bits.push_back((b >> i) & 1);
    }
    return bits;
}

// Undoes bit stuffing and finds the bytes between the first and the last
// flag.
std::vector<std::uint8_t> unframe(const std::vector<int> &bits)
{
    std::vector<std::uint8_t> bytes;
    int ones = 0;
    int bitCount = 0;
    std::uint8_t byte = 0;
    bool inFrame = false;
    std::uint8_t shift = 0;
    for (int b : bits) {
        shift = static_cast<std::uint8_t>((shift >> 1) | (b << 7));
        if (shift == HdlcBits::Flag) {
            if (inFrame && bitCount == 7 && !bytes.empty()) {
                return bytes;
            }
            inFrame = true;
            bytes.clear();
            bitCount = 0;
            byte = 0;
            ones = 0;
            continue;
        }
        if (!inFrame) {
            continue;
        }
        if (ones == 5) {
            ones = 0;
            if (b == 0) {
                continue; // stuffed
            }
        }
        ones = b == 1 ? ones + 1 : 0;
        byte = static_cast<std::uint8_t>((byte >> 1) | (b << 7));
        if (++bitCount == 8) {
            bytes.push_back(byte);
            bitCount = 0;
        }
    }
    return {};
}

} // namespace

TEST_CASE("hdlc bits: flags unstuffed, frame bytes LSB first with a 0 after five 1s")
{
    const std::array<std::uint8_t, 2> frame{0xFF, 0x01};
    HdlcBits h;
    h.start(frame, 2, 1);
    std::vector<int> expected;
    for (int i = 0; i < 2; ++i) {
        const auto f = byteBits(HdlcBits::Flag);
        expected.insert(expected.end(), f.begin(), f.end());
    }
    for (int b : {1, 1, 1, 1, 1, 0, 1, 1, 1}) {
        expected.push_back(b);
    }
    for (int b : {1, 0, 0, 0, 0, 0, 0, 0}) {
        expected.push_back(b);
    }
    const auto f = byteBits(HdlcBits::Flag);
    expected.insert(expected.end(), f.begin(), f.end());
    CHECK(allBits(h) == expected);
    CHECK(h.next() == -1);
}

TEST_CASE("hdlc bits: a stuffed 0 at the end of the frame comes before the closing flag")
{
    const std::array<std::uint8_t, 1> frame{0xF8}; // 0001 1111 LSB first: 0,0,0,1,1,1,1,1
    HdlcBits h;
    h.start(frame, 1, 1);
    std::vector<int> bits = allBits(h);
    REQUIRE(bits.size() == 8 + 9 + 8);
    CHECK(std::vector<int>(bits.begin() + 8, bits.begin() + 17) == std::vector<int>{0, 0, 0, 1, 1, 1, 1, 1, 0});
    CHECK(unframe(bits) == std::vector<std::uint8_t>{0xF8});
}

TEST_CASE("afsk modulates a frame that a tone detector reads back")
{
    ax25::Frame f;
    f.source = *ax25::parseAddress("N0CALL-7");
    f.destination = *ax25::parseAddress("APZDMM");
    f.path.pushBack(*ax25::parseAddress("WIDE1-1"));
    f.info << "!6010.25N/02456.30E>Test ~~~ \x7f\xff";
    std::array<std::uint8_t, ax25::MaxFrameBytes> bytes{};
    const std::size_t n = ax25::encode(f, bytes);
    REQUIRE(n > 0);

    constexpr std::uint32_t Rate = 32'000;
    constexpr std::uint8_t Peak = 200;
    AfskModulator m;
    m.start({bytes.data(), n}, 10, 2, Rate, Peak);
    std::vector<double> samples;
    while (m.running() && samples.size() < 1'000'000) {
        samples.push_back(m.next());
    }
    CHECK(m.next() == 0);

    // Bits stuffed into the frame: the count from the stream itself.
    HdlcBits h;
    h.start({bytes.data(), n}, 10, 2);
    const std::size_t bitCount = allBits(h).size();
    const double expectedSamples = static_cast<double>(bitCount) * Rate / AfskModulator::BaudRate;
    CHECK(std::abs(static_cast<double>(samples.size()) - expectedSamples) <= 2.0);

    double mean = 0;
    for (double s : samples) {
        mean += s;
    }
    mean /= static_cast<double>(samples.size());

    // Per bit, which tone has more energy; then NRZI: no change is a 1.
    const std::uint64_t bitStep = (static_cast<std::uint64_t>(AfskModulator::BaudRate) << 32) / Rate;
    std::vector<int> tones;
    std::size_t i = 0;
    while (i < samples.size()) {
        const std::uint64_t bit = (i * bitStep) >> 32;
        std::size_t end = i;
        while (end < samples.size() && ((end * bitStep) >> 32) == bit) {
            ++end;
        }
        double energy[2] = {};
        for (int t = 0; t < 2; ++t) {
            const double hz = t == 0 ? AfskModulator::MarkHz : AfskModulator::SpaceHz;
            double re = 0;
            double im = 0;
            for (std::size_t k = i; k < end; ++k) {
                const double w = 2 * 3.14159265358979 * hz * static_cast<double>(k) / Rate;
                re += (samples[k] - mean) * std::cos(w);
                im += (samples[k] - mean) * std::sin(w);
            }
            energy[t] = re * re + im * im;
        }
        tones.push_back(energy[1] > energy[0] ? 1 : 0);
        i = end;
    }
    REQUIRE(tones.size() == bitCount);
    std::vector<int> bits;
    int previous = 0; // the modulator starts on mark
    for (int t : tones) {
        bits.push_back(t == previous ? 1 : 0);
        previous = t;
    }
    const std::vector<std::uint8_t> back = unframe(bits);
    ax25::Frame decoded;
    REQUIRE(ax25::decode(back, decoded));
    CHECK(decoded.info.view() == f.info.view());
    CHECK(ax25::addressText(decoded.source).view() == "N0CALL-7");
}
