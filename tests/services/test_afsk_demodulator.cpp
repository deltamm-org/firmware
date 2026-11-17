// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "afsk_signal.h"

#include "dmm/services/afsk.h"
#include "dmm/services/afsk_demodulator.h"
#include "dmm/services/ax25.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace dmm::services;

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Decoded
{
    std::vector<Bytes> frames;
    std::vector<AfskDemodulator::FrameInfo> infos;

    int count(const Bytes &f) const { return static_cast<int>(std::count(frames.begin(), frames.end(), f)); }
};

Decoded decode(AfskDemodulator &d, const std::vector<std::uint16_t> &mv)
{
    Decoded out;
    d.process(std::span<const std::uint16_t>(mv), [&out](std::span<const std::uint8_t> f, const AfskDemodulator::FrameInfo &info) {
        out.frames.emplace_back(f.begin(), f.end());
        out.infos.push_back(info);
    });
    return out;
}

struct Condition
{
    double twistDb = 0.0;
    double snrDb = 99.0;
    double clockScale = 1.0;
    std::uint16_t leadFlags = 20;
    bool fmNoise = false;
    bool repair = false;
    /// The tones' peak at the sampling point, in mV.
    double peakMv = 300.0;
};

// How many of @p frames different frames sent with gaps between come out.
int decodedOf(const Condition &c, int frames, unsigned seed = 1)
{
    AfskSignal signal;
    signal.setTwist(c.twistDb);
    signal.clockScale = c.clockScale;
    std::vector<Bytes> sent;
    signal.silence(2000);
    for (int i = 0; i < frames; ++i) {
        sent.push_back(testFrame(seed * 1000 + static_cast<unsigned>(i)));
        signal.frame(sent.back(), c.leadFlags, 2);
        signal.silence(1500 + static_cast<std::size_t>(i * 37 % 700));
    }
    if (c.snrDb < 99.0) {
        signal.addNoise(c.snrDb, seed * 7 + 3, c.fmNoise);
    }
    AfskDemodulator d;
    d.setRepair(c.repair);
    const Decoded out = decode(d, signal.millivolts(c.peakMv));
    int found = 0;
    for (const Bytes &f : sent) {
        found += out.count(f) == 1 ? 1 : 0;
    }
    // Nothing that was not sent, and nothing twice.
    CHECK(out.frames.size() == static_cast<std::size_t>(found));
    return found;
}

} // namespace

TEST_CASE("afsk demodulator: a clean frame comes out once, though several slicers find it")
{
    AfskSignal signal;
    const Bytes frame = testFrame(1);
    signal.silence(500);
    signal.frame(frame, 20, 2);
    signal.silence(500);
    AfskDemodulator d;
    const Decoded out = decode(d, signal.millivolts(300.0));
    REQUIRE(out.frames.size() == 1);
    CHECK(out.frames[0] == frame);
    CHECK_FALSE(out.infos[0].repaired);
    CHECK(d.stats().passed == 1);
    CHECK(d.stats().duplicates == AfskDemodulator::Slicers - 1);
    for (std::uint32_t n : d.stats().frames) {
        CHECK(n == 1);
    }
    ax25::Frame f;
    CHECK(ax25::decode(out.frames[0], f));
}

TEST_CASE("afsk demodulator: reads what the transmit-side modulator makes at 9600 Hz")
{
    const Bytes frame = testFrame(2, 120);
    AfskModulator m;
    m.start(frame, 10, 2, AfskDemodulator::SampleRateHz, 200);
    std::vector<std::uint16_t> mv(800, 1100);
    while (m.running()) {
        mv.push_back(static_cast<std::uint16_t>(1000 + m.next()));
    }
    mv.insert(mv.end(), 800, 1100);
    AfskDemodulator d;
    const Decoded out = decode(d, mv);
    REQUIRE(out.frames.size() == 1);
    CHECK(out.frames[0] == frame);
}

TEST_CASE("afsk demodulator: decodes through noise, twist and a clock offset")
{
    CHECK(decodedOf({}, 20) == 20);
    CHECK(decodedOf({.snrDb = 12.0}, 40) == 40);
    CHECK(decodedOf({.snrDb = 8.0}, 40) >= 39);
    CHECK(decodedOf({.snrDb = 6.0}, 40) >= 36);
    CHECK(decodedOf({.snrDb = 3.0, .fmNoise = true}, 40) >= 30);
    CHECK(decodedOf({.twistDb = 6.0, .snrDb = 10.0}, 40) == 40);
    CHECK(decodedOf({.twistDb = -6.0, .snrDb = 10.0}, 40) == 40);
    CHECK(decodedOf({.twistDb = 9.0, .snrDb = 12.0}, 40) == 40);
    CHECK(decodedOf({.snrDb = 10.0, .clockScale = 1.005}, 40) == 40);
    CHECK(decodedOf({.snrDb = 10.0, .clockScale = 0.995}, 40) == 40);
    CHECK(decodedOf({.snrDb = 12.0, .clockScale = 1.015}, 40) >= 38);
}

TEST_CASE("afsk demodulator: decodes quiet and loud audio, and audio that clips")
{
    CHECK(decodedOf({.snrDb = 20.0, .peakMv = 30.0}, 40) == 40);
    CHECK(decodedOf({.snrDb = 12.0, .peakMv = 100.0}, 40) == 40);
    CHECK(decodedOf({.snrDb = 12.0, .peakMv = 1000.0}, 40) == 40);
    // Clipped at the bottom of the input's range.
    CHECK(decodedOf({.snrDb = 20.0, .peakMv = 1600.0}, 40) == 40);
    CHECK(decodedOf({.twistDb = 6.0, .snrDb = 12.0, .peakMv = 1600.0}, 40) >= 38);
}

TEST_CASE("afsk demodulator: a short preamble is enough")
{
    CHECK(decodedOf({.snrDb = 20.0, .leadFlags = 2}, 40) == 40);
    CHECK(decodedOf({.snrDb = 20.0, .leadFlags = 1}, 40) >= 28);
}

TEST_CASE("afsk demodulator: frames back to back, one flag apart")
{
    AfskSignal signal;
    const Bytes first = testFrame(3);
    const Bytes second = testFrame(4, 30);
    signal.silence(500);
    signal.frame(first, 10, 0);
    signal.frame(second, 1, 2);
    signal.silence(500);
    signal.addNoise(15.0, 5);
    AfskDemodulator d;
    const Decoded out = decode(d, signal.millivolts(300.0));
    REQUIRE(out.frames.size() == 2);
    CHECK(out.frames[0] == first);
    CHECK(out.frames[1] == second);
}

TEST_CASE("afsk demodulator: repairs one wrong bit or one wrong tone only when asked")
{
    const Bytes frame = testFrame(5);
    for (const int width : {1, 2}) {
        Bytes damaged = frame;
        // A bit in the info field; two neighbouring bits for a wrong tone.
        const std::size_t bit = 8 * 40 + 3;
        for (int i = 0; i < width; ++i) {
            damaged[(bit + i) / 8] = static_cast<std::uint8_t>(damaged[(bit + i) / 8] ^ (1u << ((bit + i) % 8)));
        }
        AfskSignal signal;
        signal.silence(500);
        signal.frame(damaged, 20, 2);
        signal.silence(500);
        const std::vector<std::uint16_t> mv = signal.millivolts(300.0);

        AfskDemodulator plain;
        CHECK(decode(plain, mv).frames.empty());
        CHECK(plain.stats().failed[2] == 1);

        AfskDemodulator repairing;
        repairing.setRepair(true);
        const Decoded out = decode(repairing, mv);
        REQUIRE(out.frames.size() == 1);
        CHECK(out.frames[0] == frame);
        CHECK(out.infos[0].repaired);
        CHECK(repairing.stats().repaired == 1);
    }
}

TEST_CASE("afsk demodulator: repairs of damaged copies do not get through beside the right frame")
{
    CHECK(decodedOf({.snrDb = 8.0, .repair = true}, 40) == 40);
    CHECK(decodedOf({.twistDb = 6.0, .snrDb = 8.0, .repair = true}, 40) == 40);
}

TEST_CASE("afsk demodulator: a repair must give a well-formed UI header")
{
    Bytes frame = testFrame(6);
    // Control byte not UI, with a right check: passed on as it is.
    frame[7 * 4] = 0x13;
    const std::uint16_t check = ax25::frameCheck({frame.data(), frame.size() - 2});
    frame[frame.size() - 2] = static_cast<std::uint8_t>(check & 0xFF);
    frame[frame.size() - 1] = static_cast<std::uint8_t>(check >> 8);
    Bytes damaged = frame;
    damaged[50] ^= 0x10;
    AfskSignal signal;
    signal.silence(500);
    signal.frame(damaged, 20, 2);
    signal.silence(500);
    AfskDemodulator d;
    d.setRepair(true);
    CHECK(decode(d, signal.millivolts(300.0)).frames.empty());
}

TEST_CASE("afsk demodulator: noise alone gives no frames")
{
    AfskSignal signal;
    signal.silence(9600 * 30);
    signal.addNoise(0.0, 9);
    AfskDemodulator d;
    d.setRepair(true);
    CHECK(decode(d, signal.millivolts(300.0)).frames.empty());
}

TEST_CASE("recent frames: a copy within the window is seen, later it is new")
{
    const Bytes a = testFrame(7);
    const Bytes b = testFrame(8);
    ax25::RecentFrames recent;
    CHECK_FALSE(recent.seen(a, 1000, 500));
    CHECK(recent.seen(a, 1400, 500));
    CHECK_FALSE(recent.seen(b, 1400, 500));
    CHECK_FALSE(recent.seen(a, 1600, 500));
    CHECK(recent.seen(a, 1700, 500));
}
