// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/radio/rda1846.h"

#include <doctest/doctest.h>

#include <bit>
#include <map>
#include <vector>

using dmm::drivers::radio::Rda1846;

namespace {

class FakeBus final : public dmm::hal::RegisterBus
{
public:
    std::uint16_t read(std::uint8_t address) override { return registers[address]; }
    void write(std::uint8_t address, std::uint16_t value) override
    {
        registers[address] = value;
        writes.push_back(address);
    }

    std::map<std::uint8_t, std::uint16_t> registers;
    std::vector<std::uint8_t> writes;
};

class LoggingBus final : public dmm::hal::RegisterBus
{
public:
    std::uint16_t read(std::uint8_t address) override { return registers[address]; }
    void write(std::uint8_t address, std::uint16_t value) override
    {
        registers[address] = value;
        log.emplace_back(address, value);
    }

    std::map<std::uint8_t, std::uint16_t> registers;
    std::vector<std::pair<std::uint8_t, std::uint16_t>> log;
};

unsigned sleptMs = 0;
void sleep(unsigned ms)
{
    sleptMs += ms;
}

using Write = std::pair<std::uint8_t, std::uint16_t>;

} // namespace

TEST_CASE("the RDA1846 is found by its chip ID")
{
    FakeBus bus;
    Rda1846 chip(bus);
    CHECK_FALSE(chip.present());
    bus.registers[0x00] = 0x1846;
    CHECK(chip.present());
    CHECK(bus.writes.empty());
}

TEST_CASE("setGpio changes one pin's two bits and keeps the rest")
{
    FakeBus bus;
    Rda1846 chip(bus);
    bus.registers[0x1F] = 0x5AA2;
    CHECK(chip.setGpio(3, Rda1846::GpioMode::High) == 0x5AE2);
    CHECK(bus.registers[0x1F] == 0x5AE2);
    CHECK(chip.setGpio(0, Rda1846::GpioMode::HighImpedance) == 0x5AE0);
    CHECK(chip.setGpio(7, Rda1846::GpioMode::Low) == 0x9AE0);
    CHECK(bus.writes == std::vector<std::uint8_t>{0x1F, 0x1F, 0x1F});
    // An out-of-range pin writes nothing.
    chip.setGpio(8, Rda1846::GpioMode::High);
    CHECK(bus.writes.size() == 3);
}

TEST_CASE("frequency and squelch words follow the programming guide's examples")
{
    CHECK(Rda1846::frequencyWord(409'750'000) == 0x640960);
    CHECK(Rda1846::frequencyWord(145'500'000) == 145'500u * 16);
    CHECK(Rda1846::squelchWord(-120, -122) == 0x088F);
    // Levels past the field's range are clamped.
    CHECK(Rda1846::squelchWord(-140, 0) == 0x007F);
}

TEST_CASE("start resets, restores the GPIO pins before calibrating, and leaves the chip idle")
{
    LoggingBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    sleptMs = 0;
    REQUIRE(chip.start(sleep, 0x00C0));
    REQUIRE(bus.log.size() > 4);
    CHECK(bus.log[0] == Write{0x30, 0x0001});
    CHECK(bus.log[1] == Write{0x30, 0x0004});
    CHECK(bus.log[2] == Write{0x1F, 0x00C0});
    CHECK(bus.registers[0x30] == 0x4006);
    CHECK(bus.registers[0x04] == 0x0FD0);
    CHECK(bus.registers[0x7F] == 0);
    CHECK(sleptMs == 210);
    CHECK(chip.mode() == Rda1846::Mode::Idle);

    LoggingBus none;
    Rda1846 missing(none);
    CHECK_FALSE(missing.start(sleep, 0));
    CHECK(none.log.empty());
}

namespace {

// A small tuning that exercises every list the driver walks.
constexpr Rda1846::Setting TestStartUp[] = {{0x04, 0x0FD0}, {0x47, 0x7F2F}};
constexpr Rda1846::Setting TestAfterCalibration[] = {{0x27, 0x74A4, 5}, {0x27, 0x6CA4}};
constexpr Rda1846::WidthSetting TestWidth[] = {{0x32, 0x4495, 0x75E3}};
constexpr Rda1846::WidthSetting TestWidthPage1[] = {{0x06, 0x0014, 0x0024}};
constexpr Rda1846::RangeSetting TestRanges[] = {{200'000'000, 299'999'999, {0x34, 0x2B8E, 0x2B8B}}};
constexpr Rda1846::WidthSetting TestReceive[] = {{0x0A, 0x4380, 0x43A0}, {0x58, 0x8085, 0x8085}};
constexpr Rda1846::WidthSetting TestTransmit[] = {{0x0A, 0x3BA0, 0x3BA0}, {0x58, 0x8405, 0x8405}};
constexpr Rda1846::BandWord TestBandWords[] = {
    {145'000'000, 145'000'000, 0x8643},
    {144'000'000, 146'000'000, 0x8543},
    {145'000'000, 145'000'000, 0x1111}, // never reached: the first match wins
};
constexpr Rda1846::Tuning TestTuning{
    .startUp = TestStartUp,
    .afterCalibration = TestAfterCalibration,
    .width = TestWidth,
    .widthPage1 = TestWidthPage1,
    .ranges = TestRanges,
    .receive = TestReceive,
    .transmit = TestTransmit,
    .bandWords = TestBandWords,
    .normalBand = 0x8763,
};

} // namespace

TEST_CASE("receive and transmit always pass through idle")
{
    LoggingBus bus;
    Rda1846 maker(bus);
    maker.setMode(Rda1846::Mode::Receive);
    CHECK(bus.log == std::vector<Write>{{0x30, 0x4006}, {0x30, 0x4026}});
    bus.log.clear();

    Rda1846 chip(bus, TestTuning);
    chip.setMode(Rda1846::Mode::Receive);
    CHECK(bus.log == std::vector<Write>{{0x30, 0x4006}, {0x0A, 0x4380}, {0x58, 0x8085}, {0x30, 0x4026}});
    chip.setBandwidth(Rda1846::Bandwidth::Wide);
    bus.log.clear();
    chip.setMode(Rda1846::Mode::Receive);
    CHECK(bus.log == std::vector<Write>{{0x30, 0x7006}, {0x0A, 0x43A0}, {0x58, 0x8085}, {0x30, 0x7026}});
    bus.log.clear();
    chip.setMode(Rda1846::Mode::Transmit);
    CHECK(bus.log == std::vector<Write>{{0x30, 0x7006}, {0x0A, 0x3BA0}, {0x58, 0x8405}, {0x30, 0x7046}});
    bus.log.clear();
    chip.setMode(Rda1846::Mode::Idle);
    CHECK(bus.log == std::vector<Write>{{0x30, 0x7006}});
}

TEST_CASE("start writes the tuning's lists around the calibration run, with their waits")
{
    LoggingBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus, TestTuning);
    sleptMs = 0;
    REQUIRE(chip.start(sleep, 0));
    CHECK(sleptMs == 215);
    CHECK(bus.registers[0x47] == 0x7F2F);
    std::vector<Write> calibrated;
    for (std::size_t i = 0; i < bus.log.size(); ++i) {
        if (bus.log[i] == Write{0x30, 0x40A6}) {
            calibrated.assign(bus.log.begin() + static_cast<std::ptrdiff_t>(i) + 1, bus.log.end());
        }
    }
    REQUIRE(calibrated.size() >= 3);
    CHECK(calibrated[0] == Write{0x30, 0x4006});
    CHECK(calibrated[1] == Write{0x27, 0x74A4});
    CHECK(calibrated[2] == Write{0x27, 0x6CA4});
    CHECK(bus.registers[0x32] == 0x4495); // narrow after start
}

TEST_CASE("width settings for a frequency range follow the width settings")
{
    LoggingBus bus;
    Rda1846 chip(bus, TestTuning);
    chip.setFrequency(220'000'000);
    bus.log.clear();
    chip.setBandwidth(Rda1846::Bandwidth::Wide);
    CHECK(bus.log == std::vector<Write>{{0x32, 0x75E3}, {0x7F, 1}, {0x06, 0x0024}, {0x7F, 0}, {0x34, 0x2B8B}});
    chip.setFrequency(145'500'000);
    bus.log.clear();
    chip.setBandwidth(Rda1846::Bandwidth::Narrow);
    CHECK(bus.log == std::vector<Write>{{0x32, 0x4495}, {0x7F, 1}, {0x06, 0x0014}, {0x7F, 0}});
}

TEST_CASE("the first band word whose range holds the frequency wins")
{
    LoggingBus bus;
    Rda1846 chip(bus, TestTuning);
    chip.setFrequency(145'000'000);
    CHECK(bus.registers[0x05] == 0x8643);
    chip.setFrequency(145'500'000);
    CHECK(bus.registers[0x05] == 0x8543);
    chip.setFrequency(146'000'001);
    CHECK(bus.registers[0x05] == 0x8763);
}

TEST_CASE("tuning picks the band setting near the reference harmonics")
{
    LoggingBus bus;
    Rda1846 chip(bus);
    chip.setFrequency(433'500'000);
    CHECK(bus.registers[0x05] == 0x8763);
    CHECK(bus.registers[0x29] == (Rda1846::frequencyWord(433'500'000) >> 16));
    CHECK(bus.registers[0x2A] == (Rda1846::frequencyWord(433'500'000) & 0xFFFF));
    chip.setFrequency(455'025'000);
    CHECK(bus.registers[0x05] == 0x8543);
    chip.setFrequency(145'500'000);
    CHECK(bus.registers[0x05] == 0x8763);
}

TEST_CASE("RSSI, transmit tone and deviation")
{
    LoggingBus bus;
    Rda1846 chip(bus);
    bus.registers[0x1B] = 0x2A40; // 42 dB over the floor, noise 64 dB
    CHECK(chip.rssiDbm() == -95);
    CHECK(chip.noiseDb() == 64);
    chip.setNoiseSquelch(56, 53);
    CHECK(bus.registers[0x48] == ((56 << 7) | 53));
    using Tone = Rda1846::SubTone;
    chip.setTransmitTone(Tone{Tone::Kind::Ctcss, 11'880, false});
    CHECK(bus.registers[0x4A] == 11'880);
    CHECK(bus.registers[0x4E] == 0x2602);
    chip.setTransmitTone(Tone{});
    CHECK(bus.registers[0x4E] == 0x2002);
    chip.setDeviation(0x50, 0x12);
    CHECK(bus.registers[0x59] == ((0x50 << 6) | 0x12));
}

TEST_CASE("DCS words match the chip maker's example and are Golay codewords")
{
    // The programming guide's example: DCS 023 is 0x76 in 0x4B and 0x3813
    // in 0x4C.
    CHECK(Rda1846::dcsWord(023) == 0x763813u);
    // Any two Golay (23,12) codewords differ in at least seven bits.
    for (const std::uint16_t a : {std::uint16_t{023}, std::uint16_t{0114}, std::uint16_t{0245}, std::uint16_t{0754}}) {
        for (const std::uint16_t b : {std::uint16_t{025}, std::uint16_t{0115}, std::uint16_t{0251}, std::uint16_t{0731}}) {
            const std::uint32_t diff = Rda1846::dcsWord(a) ^ Rda1846::dcsWord(b);
            CHECK(std::popcount(diff) >= 7);
        }
        CHECK((Rda1846::dcsWord(a) & 0xFFF) == (0x800u | a));
    }
}

TEST_CASE("DCS transmit tones")
{
    LoggingBus bus;
    Rda1846 chip(bus);
    using Tone = Rda1846::SubTone;
    chip.setTransmitTone(Tone{Tone::Kind::Dcs, 023, false});
    CHECK(bus.registers[0x4A] == 13'440);
    CHECK(bus.registers[0x4B] == 0x0076);
    CHECK(bus.registers[0x4C] == 0x3813);
    CHECK(bus.registers[0x4E] == 0x2482);
    chip.setTransmitTone(Tone{Tone::Kind::Dcs, 023, true});
    CHECK(bus.registers[0x4E] == 0x2582);
}

TEST_CASE("a receive tone switches on its own detector and flag, kept across width changes")
{
    LoggingBus bus;
    Rda1846 chip(bus);
    using Tone = Rda1846::SubTone;
    bus.registers[0x3A] = 0x40C3;
    chip.setReceiveTone(Tone{Tone::Kind::Ctcss, 8850, false});
    CHECK(bus.registers[0x4A] == 8850);
    CHECK(bus.registers[0x3A] == 0x40C1);
    CHECK(chip.receiveToneHeard(Rda1846::CtcssMatch));
    CHECK_FALSE(chip.receiveToneHeard(Rda1846::DcsMatch | 1u << 2));
    chip.setBandwidth(Rda1846::Bandwidth::Wide);
    CHECK(bus.registers[0x3A] == 0x44C1);
    chip.setReceiveTone(Tone{Tone::Kind::Dcs, 0754, true});
    CHECK(bus.registers[0x3A] == 0x44C4);
    CHECK(bus.registers[0x4E] == 0x2582);
    CHECK(chip.receiveToneHeard(Rda1846::DcsInvertedMatch));
    CHECK_FALSE(chip.receiveToneHeard(Rda1846::DcsMatch));
    chip.setReceiveTone(Tone{Tone::Kind::Dcs, 023, false});
    CHECK(chip.receiveToneHeard(Rda1846::DcsMatch));
    chip.setReceiveTone(Tone{});
    CHECK(bus.registers[0x3A] == 0x44C0);
    CHECK(bus.registers[0x4E] == 0x2002);
    CHECK_FALSE(chip.receiveToneHeard(0xFFFF));
}

TEST_CASE("the tuning table's detector bits give way to the driver's")
{
    LoggingBus bus;
    Rda1846 chip(bus);
    chip.setBandwidth(Rda1846::Bandwidth::Narrow);
    CHECK((bus.registers[0x3A] & 0x1F) == 0);
}
