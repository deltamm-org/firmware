// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "../drivers/fake_sct3258.h"
#include "dmm/services/sct3258_baseband.h"

#include <doctest/doctest.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

using dmm::drivers::baseband::Sct3258;
using dmm::services::Sct3258Baseband;

namespace {

void noDelay(unsigned) {}

class FakeBoard final : public dmm::services::BasebandBoard
{
public:
    int powerUps = 0;

    void powerUpAndReset() override { ++powerUps; }
};

const std::vector<std::uint8_t> Blob(2048, 0x22);

using CodecWrite = Sct3258Baseband::CodecWrite;
constexpr CodecWrite Receive[] = {{1, 52, 0x00}, {0, 65, 0xD4}};
constexpr CodecWrite Transmit[] = {{0, 65, 0x00}, {1, 59, 32}, {0, 86, 0xA0}, {0, 88, 32}, {1, 52, 0x80}};
constexpr Sct3258Baseband::Audio TestAudio{
    .codec = Sct3258::CodecTlv320aic3204,
    .receive = Receive,
    .transmit = Transmit,
    .speakerPage = 1,
    .speakerRegister = 0x10,
    .speakerMuted = 0x40,
    .speakerSteps = {0x3A, 0x3C, 0x3F, 2, 6, 8, 10, 12, 13, 15, 16, 18, 20, 22, 23},
    .trims = {{{1, 0x12}, {1, 0x13}, {1, 0x3C}}},
    .digitalSquelch = {},
    .analogSquelch = {},
};

std::optional<Sct3258Baseband::Trims> testTrims()
{
    return Sct3258Baseband::Trims{{0x46, 0x50, 0x05, 0x78, 0x09, 0xC4, 0x05, 0xDC, 0x01, 0x0E}, {4, 8, 12}};
}

// Trims that appear once the calibration has been read.
bool calibrationRead = false;
std::optional<Sct3258Baseband::Trims> laterTrims()
{
    return calibrationRead ? testTrims() : std::nullopt;
}

} // namespace

TEST_CASE("start boots the chip, reads its versions, picks the codec and leaves it in analog mode")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);

    REQUIRE(baseband.start());
    CHECK(board.powerUps == 1);
    CHECK(chip.bootBytes == 2048);
    const auto info = baseband.info();
    CHECK(info.running);
    CHECK(info.problem == nullptr);
    CHECK(std::strcmp(info.product, "CT3258F") == 0);
    CHECK(std::strcmp(info.software, "V2.01.07") == 0);

    REQUIRE(chip.packets.size() == 11);
    CHECK(chip.packets[3] == std::vector<std::uint8_t>{0x00, 0x2D, 0x01});
    CHECK(chip.packets[4] == std::vector<std::uint8_t>{0x00, 0x10, 0x04});
    CHECK(chip.packets[5] == std::vector<std::uint8_t>{0x00, 0x10, 0x10});
    CHECK(chip.packets[6] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 52, 0x00, 0x00, 0x00}); // mic off
    CHECK(chip.packets[7] == std::vector<std::uint8_t>{0x40, 0x00, 0x00, 65, 0xD4, 0x00, 0x00}); // DAC -22 dB
    // The calibrated codec trims.
    CHECK(chip.packets[8] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x12, 0x04, 0x00, 0x00});
    CHECK(chip.packets[10] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x3C, 0x0C, 0x00, 0x00});
}

TEST_CASE("without a boot program or an answer the baseband reports why and stays off")
{
    {
        FakeSct3258 chip;
        FakeBoard board;
        Sct3258 sct(chip, noDelay);
        Sct3258Baseband baseband(sct, board, {}, TestAudio, testTrims);
        CHECK_FALSE(baseband.start());
        CHECK(board.powerUps == 0);
        CHECK(baseband.info().problem != nullptr);
        CHECK_FALSE(baseband.setSpeakerVolume(5));
        CHECK_FALSE(baseband.playTone(1000, 100, 50));
    }
    {
        FakeSct3258 chip;
        chip.answerBoot = false;
        FakeBoard board;
        Sct3258 sct(chip, noDelay);
        Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
        CHECK_FALSE(baseband.start());
        CHECK(board.powerUps == 1);
        CHECK_FALSE(baseband.info().running);
        CHECK(chip.packets.empty());
    }
}

TEST_CASE("volume steps come from the board's table")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    const Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    CHECK(baseband.volumeRegister(0) == 0x40);
    CHECK(baseband.volumeRegister(1) == 0x3A);
    CHECK(baseband.volumeRegister(15) == 23);
    CHECK(baseband.volumeRegister(99) == 23);
}

TEST_CASE("volume, tone, codec writes and audio pins after start")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    REQUIRE(baseband.start());
    chip.packets.clear();

    CHECK(baseband.setSpeakerVolume(15));
    CHECK(baseband.playTone(1000, 300, 50));
    CHECK(baseband.codecWrite(1, 0x12, 0x04));
    REQUIRE(chip.packets.size() == 3);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x10, 0x17, 0x00, 0x00});
    CHECK(chip.packets[1] == std::vector<std::uint8_t>{0x00, 0x1F, 0x01, 0x03, 0xE8, 0, 0, 0, 0, 0, 0, 0x20, 0, 0, 0, 0x01, 0x2C, 0, 1});
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x12, 0x04, 0x00, 0x00});
}

TEST_CASE("analog mode, work mode and carrier packets")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    CHECK_FALSE(baseband.setAnalog(false)); // not started
    REQUIRE(baseband.start());
    chip.packets.clear();

    CHECK(baseband.setAnalog(true));
    CHECK(baseband.setWorkMode(dmm::services::BasebandWorkMode::Receive));
    CHECK(baseband.setCarrier(true));
    // The codec trims went in at start.
    REQUIRE(chip.packets.size() == 4);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x00, 0x1A, 0x80});
    CHECK(chip.packets[1] ==
        std::vector<std::uint8_t>{0x00, 0x3C, 0xC2, 0x46, 0x50, 0x05, 0x78, 0x09, 0xC4, 0x05, 0xDC, 0x01, 0x0E});
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x00, 0x18, 0x01, 0x00, 0x00});
    CHECK(chip.packets[3] == std::vector<std::uint8_t>{0x00, 0x19, 0x01});
    chip.packets.clear();
    CHECK(baseband.setAnalog(false));
    CHECK(chip.packets.size() == 2);
}

TEST_CASE("analog mode needs the calibrated values")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, [] { return std::optional<Sct3258Baseband::Trims>{}; });
    REQUIRE(baseband.start());
    chip.packets.clear();
    CHECK_FALSE(baseband.setAnalog(true));
    CHECK(chip.packets.empty());
}

TEST_CASE("codec trims read after start go in with the first analog set-up, once")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    calibrationRead = false;
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, laterTrims);
    REQUIRE(baseband.start());
    CHECK(chip.packets.size() == 8);
    chip.packets.clear();

    calibrationRead = true;
    CHECK(baseband.setAnalog(false));
    REQUIRE(chip.packets.size() == 5);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x12, 0x04, 0x00, 0x00});
    chip.packets.clear();
    CHECK(baseband.setAnalog(false));
    CHECK(chip.packets.size() == 2);
}

TEST_CASE("microphone path")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    CHECK_FALSE(baseband.setMicrophone(true)); // not started
    REQUIRE(baseband.start());
    chip.packets.clear();

    CHECK(baseband.setMicrophone(true));
    REQUIRE(chip.packets.size() == 5);
    // The microphone is connected last, after its gain is set.
    CHECK(chip.packets[4] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x34, 0x80, 0x00, 0x00});
    chip.packets.clear();
    CHECK(baseband.setMicrophone(false));
    REQUIRE(chip.packets.size() == 2);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00});
}

TEST_CASE("the chip's console commands write codec registers and send raw packets")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    REQUIRE(baseband.start());
    chip.packets.clear();

    CHECK(baseband.debugCommand("codec 1 12 4"));
    REQUIRE(chip.packets.size() == 1);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x12, 0x04, 0x00, 0x00});
    CHECK(baseband.debugCommand("packet 20 30"));
    REQUIRE(chip.packets.size() == 2);
    CHECK(chip.packets[1] == std::vector<std::uint8_t>{0x20, 0x30});
    CHECK_FALSE(baseband.debugCommand("codec 1 80 4")); // register out of range
    CHECK_FALSE(baseband.debugCommand("codec 1 12"));
    CHECK_FALSE(baseband.debugCommand("packet"));
    CHECK_FALSE(baseband.debugCommand("restart"));
}

TEST_CASE("digital receive: vocoder, layer 2, call options once, colour code, slot, receive")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    REQUIRE(baseband.start());
    chip.packets.clear();

    dmm::services::dmr::ChannelSettings settings;
    settings.colourCode = 7;
    settings.slot = 2;
    REQUIRE(baseband.setDigital(settings, 433'450'000, true));
    REQUIRE(chip.packets.size() == 7);
    // Without a calibration source, no DC offset or demodulator gain.
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x00, 0x10, 0x0C});
    CHECK(chip.packets[1] == std::vector<std::uint8_t>{0x00, 0x1A, 0x02});
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x08, 0x79, 0x41, 0x00, 0x13, 0x0C, 0x00});
    CHECK(chip.packets[3] == std::vector<std::uint8_t>{0x08, 0x77, 0x07});
    CHECK(chip.packets[4] == std::vector<std::uint8_t>{0x08, 0x6F, 0x03});
    CHECK(chip.packets[5] == std::vector<std::uint8_t>{0x00, 0x18, 0x01, 0x00, 0x00});
    CHECK(chip.packets[6] == std::vector<std::uint8_t>{0x00, 0x19, 0x01});

    // Direct: both slots; the vocoder and the call options stay.
    chip.packets.clear();
    REQUIRE(baseband.setDigital(settings, 433'450'000, false));
    REQUIRE(chip.packets.size() == 5);
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x08, 0x6F, 0x03});

    // Back to FM: the analog vocoder first.
    chip.packets.clear();
    REQUIRE(baseband.setAnalog(false));
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x00, 0x10, 0x10});
}

TEST_CASE("the chip's reports become DMR events")
{
    using Kind = dmm::services::dmr::Event::Kind;
    using dmm::services::dmr::DataType;
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims);
    REQUIRE(baseband.start());

    chip.queue(0x30, {0x7F, 0xA9}); // repeater timing, verified, inbound busy, next slot 1
    chip.queue(0x30, {0x1B, 0x05}); // voice sync: passed on as an undecoded report
    chip.queue(0x30, {0x77, 0x01, 0x43, 0x01, 0x09, 0x00, 0x00, 0x00, 0x00, 0x09, 0x89, 0x74, 0xCB, 0xB1});
    chip.queue(0x30, {0x77, 0x01, 0x43, 0x11, 0x09, 0x00, 0x00, 0x00, 0x00, 0x09, 0x89, 0x74, 0xCB, 0xB1});
    chip.queue(0x30, {0x1B, 0x08}); // timing lost
    chip.queue(0x20, {0x17, 0x0A}); // all sent

    dmm::services::dmr::Event e;
    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::SlotFound);
    CHECK(e.baseStation);
    CHECK(e.verified);
    CHECK_FALSE(e.inverted);
    CHECK(e.inboundBusy);
    CHECK(e.nextSlot == 1);

    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::Other);
    CHECK(e.data[0] == 0x1B);

    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::Burst);
    CHECK(e.colourCode == 1);
    CHECK_FALSE(e.voice);
    CHECK(e.type == DataType::VoiceLcHeader);
    REQUIRE(e.length == 9);
    const auto lc = dmm::services::dmr::FullLc::decode(e.payload());
    REQUIRE(lc.has_value());
    CHECK(lc->destination == 2441);
    CHECK(lc->source == 7654321);

    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::Burst);
    CHECK(e.voice);

    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::SyncLost);
    REQUIRE(baseband.nextEvent(e));
    CHECK(e.kind == Kind::AllSent);
    CHECK_FALSE(baseband.nextEvent(e));
}

TEST_CASE("digital receive sends the calibrated DC offset and demodulator gain")
{
    FakeSct3258 chip;
    FakeBoard board;
    Sct3258 sct(chip, noDelay);
    Sct3258Baseband baseband(sct, board, Blob, TestAudio, testTrims,
        [](std::uint32_t hz) -> std::optional<Sct3258Baseband::DigitalTrims> {
            if (hz < 400'000'000) {
                return std::nullopt;
            }
            return Sct3258Baseband::DigitalTrims{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, {0x08, 0x00}};
        });
    REQUIRE(baseband.start());
    chip.packets.clear();
    dmm::services::dmr::ChannelSettings settings;
    REQUIRE(baseband.setDigital(settings, 438'075'000, true));
    REQUIRE(chip.packets.size() == 9);
    CHECK(chip.packets[1] == std::vector<std::uint8_t>{0x00, 0x39, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x00, 0x1C, 0x08, 0x00});
    chip.packets.clear();
    REQUIRE(baseband.setDigital(settings, 145'000'000, true));
    CHECK(chip.packets.size() == 5); // no values for this band; the vocoder stays
}
