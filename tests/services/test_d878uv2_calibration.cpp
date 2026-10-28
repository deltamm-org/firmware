// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_calibration.h"

#include <doctest/doctest.h>

#include <array>

using dmm::services::D878Uv2Calibration;
using dmm::services::PowerLevel;

namespace {

constexpr std::uint32_t MHz = 1'000'000;

void put16(std::array<std::uint8_t, D878Uv2Calibration::Bytes> &d, std::size_t at, std::uint16_t v)
{
    d[at] = static_cast<std::uint8_t>(v);
    d[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// A calibrated radio with simple round numbers.
std::array<std::uint8_t, D878Uv2Calibration::Bytes> sample()
{
    std::array<std::uint8_t, D878Uv2Calibration::Bytes> d{};
    d[0x14] = 'w';
    d[0x15] = 'w';
    put16(d, 0x00, 0x0812); // reference trim
    // UHF block: power bases Low..Turbo, deviation, squelch, tune points.
    d[0x02] = 40;
    d[0x03] = 60;
    d[0x04] = 90;
    d[0x05] = 120;
    d[0x06] = 0x50;
    d[0x07] = 0x12;
    d[0x08] = 0x13;
    d[0x09] = 100;
    put16(d, 0x0A, 1000); // 400 MHz
    put16(d, 0x0C, 2000); // 440 MHz
    put16(d, 0x0E, 3000); // 480 MHz
    // VHF block.
    d[0x22] = 30;
    d[0x29] = 80;
    put16(d, 0x2A, 600);  // 136 MHz
    put16(d, 0x2C, 900);  // 155 MHz
    put16(d, 0x2E, 1500); // 174 MHz
    // UHF Low flatness: 10 everywhere, 14 at 433 MHz, 8 at 401 MHz.
    for (std::size_t i = 0; i < 121; ++i) {
        d[0x200 + i] = 10;
    }
    d[0x200 + 33] = 14;
    d[0x200 + 1] = 8;
    // VHF Low flatness: 20 everywhere, 25 at 145 MHz.
    for (std::size_t i = 0; i < 39; ++i) {
        d[0x400 + i] = 20;
    }
    d[0x400 + 9] = 25;
    // Squelch flatness: VHF all 60 but 62 at 144-145 MHz; UHF all 60 but 57 at 430-434 MHz.
    for (std::size_t i = 0; i < 20; ++i) {
        d[0x500 + i] = 60;
    }
    d[0x500 + 4] = 62;
    for (std::size_t i = 0; i < 25; ++i) {
        d[0x520 + i] = 60;
    }
    d[0x520 + 6] = 57;
    for (std::size_t i = 0; i < 10; ++i) {
        d[0x40 + i] = static_cast<std::uint8_t>(i + 1);
    }
    d[0x92] = 4;
    d[0x93] = 8;
    d[0x94] = 12;
    return d;
}

D878Uv2Calibration loaded(const std::array<std::uint8_t, D878Uv2Calibration::Bytes> &d)
{
    return D878Uv2Calibration(d);
}

} // namespace

TEST_CASE("transmit power is the band base corrected by the level's flatness")
{
    const D878Uv2Calibration cal = loaded(sample());
    CHECK(cal.txPower(460 * MHz, PowerLevel::Low) == 40 * 16);
    CHECK(cal.txPower(433'500'000, PowerLevel::Low) == 44 * 16);
    CHECK(cal.txPower(401 * MHz, PowerLevel::Low) == 38 * 16);
    CHECK(cal.txPower(155 * MHz, PowerLevel::Low) == 30 * 16);
    CHECK(cal.txPower(145'500'000, PowerLevel::Low) == 35 * 16);
    // Outside the calibrated bands: nothing.
    CHECK_FALSE(cal.txPower(200 * MHz, PowerLevel::Low).has_value());
    CHECK_FALSE(cal.txPower(399'999'999, PowerLevel::Low).has_value());
    CHECK_FALSE(cal.txPower(521 * MHz, PowerLevel::Low).has_value());
}

TEST_CASE("a power value outside 0..255 is refused, not wrapped")
{
    auto d = sample();
    d[0x200 + 50] = 0; // 450 MHz: 40 + 0 - 10 = 30, still fine
    d[0x02] = 5;       // now 5 + 0 - 10 < 0
    const D878Uv2Calibration cal = loaded(d);
    CHECK_FALSE(cal.txPower(450 * MHz, PowerLevel::Low).has_value());
    CHECK(cal.txPower(460 * MHz, PowerLevel::Low) == 5 * 16);
    // Above 255 the value no longer fits the DAC's top bits.
    d[0x02] = 250;
    d[0x200 + 50] = 20;
    CHECK_FALSE(loaded(d).txPower(450 * MHz, PowerLevel::Low).has_value());
}

TEST_CASE("front-end tuning interpolates between the calibration points")
{
    const D878Uv2Calibration cal = loaded(sample());
    CHECK(cal.rxTune(400 * MHz) == 1000);
    CHECK(cal.rxTune(420 * MHz) == 1500);
    CHECK(cal.rxTune(440 * MHz) == 2000);
    CHECK(cal.rxTune(435 * MHz) == 1875);
    CHECK(cal.rxTune(480 * MHz) == 3000);
    CHECK(cal.rxTune(500 * MHz) == 3548); // halfway to full scale (4096), integer maths
    CHECK(cal.rxTune(520 * MHz) == 0xFFF);
    CHECK(cal.rxTune(136 * MHz) == 600);
    CHECK(cal.rxTune(145'500'000) == 750);
    CHECK(cal.rxTune(174 * MHz) == 1500);
    CHECK_FALSE(cal.rxTune(300 * MHz).has_value());
}

TEST_CASE("squelch threshold: base, level and flatness")
{
    const D878Uv2Calibration cal = loaded(sample());
    CHECK(cal.squelchThreshold(456 * MHz, 1) == 100);
    CHECK(cal.squelchThreshold(456 * MHz, 3) == 96);
    CHECK(cal.squelchThreshold(432 * MHz, 1) == 103); // flatness 57 against 60
    CHECK(cal.squelchThreshold(144'800'000, 1) == 78);
    CHECK(cal.squelchThreshold(174 * MHz, 1) == 80); // last entry
    CHECK_FALSE(cal.squelchThreshold(250 * MHz, 1).has_value());
}

TEST_CASE("reference trim, deviation and baseband gains")
{
    const D878Uv2Calibration cal = loaded(sample());
    CHECK(cal.referenceTrim() == 0x0812);
    REQUIRE(cal.deviation().has_value());
    CHECK(cal.deviation()->voice == 0x50);
    CHECK(cal.deviation()->ctcss == 0x12);
    CHECK(cal.deviation()->dcs == 0x13);
    REQUIRE(cal.codecTrims().has_value());
    CHECK(cal.codecTrims()->leftOut == 4);
    CHECK(cal.codecTrims()->rightOut == 8);
    CHECK(cal.codecTrims()->rightInput == 12);
    REQUIRE(cal.basebandAnalogGains().has_value());
    CHECK((*cal.basebandAnalogGains())[0] == 1);
    CHECK((*cal.basebandAnalogGains())[9] == 10);
}

TEST_CASE("an uncalibrated radio gives no RF values")
{
    auto d = sample();
    d[0x14] = 0;
    const D878Uv2Calibration cal = loaded(d);
    CHECK_FALSE(cal.referenceTrim().has_value());
    CHECK_FALSE(cal.txPower(460 * MHz, PowerLevel::Low).has_value());
    CHECK_FALSE(cal.rxTune(440 * MHz).has_value());
    CHECK_FALSE(cal.squelchThreshold(440 * MHz, 1).has_value());
    CHECK_FALSE(cal.deviation().has_value());
}

TEST_CASE("baseband DC offset by band and frequency range, and the demodulator gain")
{
    auto d = sample();
    for (std::uint8_t i = 0; i < 16; ++i) {
        d[0x80 + i] = static_cast<std::uint8_t>(0x10 + i); // UHF block
        d[0xC0 + i] = static_cast<std::uint8_t>(0x40 + i); // VHF block
    }
    d[0x97] = 0x08;
    d[0x98] = 0x20;
    const D878Uv2Calibration cal = loaded(d);
    const auto u = cal.basebandDcOffset(438'075'000);
    REQUIRE(u.has_value());
    CHECK((*u)[0] == 0x10);
    CHECK((*u)[9] == 0x19);
    CHECK((*u)[10] == 0x1A); // below 460 MHz: the word at +10
    CHECK((*u)[11] == 0x1B);
    CHECK((*cal.basebandDcOffset(470 * MHz))[10] == 0x1C);
    CHECK((*cal.basebandDcOffset(145 * MHz))[0] == 0x40);
    CHECK((*cal.basebandDcOffset(145 * MHz))[10] == 0x4A);
    CHECK((*cal.basebandDcOffset(160 * MHz))[10] == 0x4C);
    CHECK_FALSE(cal.basebandDcOffset(300 * MHz).has_value());
    CHECK(*cal.basebandDemodGain() == std::array<std::uint8_t, 2>{0x08, 0x20});
}

TEST_CASE("the digital voice gains come from the UHF block, nothing when erased")
{
    auto d = sample();
    d[0x99] = 0x05;
    d[0x9A] = 0x40;
    const D878Uv2Calibration cal = loaded(d);
    CHECK(*cal.basebandSpeakerGain() == std::array<std::uint8_t, 2>{0x05, 0x40});
    d[0x9B] = 0xFF;
    d[0x9C] = 0xFF;
    CHECK_FALSE(loaded(d).basebandMicGain().has_value());
}

TEST_CASE("the slot timing as calibrated, or a default offset")
{
    auto d = sample();
    for (std::uint8_t i = 0; i < 8; ++i) {
        d[0x100 + i] = static_cast<std::uint8_t>(0x10 + i);
    }
    d[0x108] = 'f';
    CHECK(*loaded(d).basebandSlotTiming() == std::array<std::uint8_t, 8>{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17});
    d[0x108] = 0xFF; // not calibrated
    CHECK(*loaded(d).basebandSlotTiming() == std::array<std::uint8_t, 8>{0x00, 0x6E, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17});
}
