// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/baseband/sct3258.h"
#include "fake_sct3258.h"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

using namespace dmm::drivers::baseband;

namespace {

void noDelay(unsigned) {}

const std::vector<std::uint8_t> Blob(2048, 0x11);

} // namespace

TEST_CASE("the parser finds packets and skips padding and noise")
{
    PacketParser parser;
    Packet p;
    const std::vector<std::uint8_t> stream = {0x00, 0x84, 0x84, 0xA9, 0x61, 0x00, 0x03, 0x20, 0x31, 0x01, 0x41, 0x00,
        0x84, 0xA9, 0x61, 0x00, 0x02, 0x00, 0x10, 0x00};
    std::vector<Packet> found;
    for (std::uint8_t b : stream) {
        if (parser.feed(b, p)) {
            found.push_back(p);
        }
    }
    REQUIRE(found.size() == 2);
    CHECK(found[0].type == 0x20);
    CHECK(found[0].field(0x31).size() == 2);
    CHECK(found[1].field(0x10)[0] == 0x00);
    CHECK(found[1].field(0x31).empty());
}

TEST_CASE("boot sends the program raw and waits for ready")
{
    FakeSct3258 chip;
    Sct3258 sct(chip, noDelay);
    std::uint8_t ack = 0xFF;
    CHECK(sct.boot(Blob, ack) == Sct3258::BootResult::Ready);
    CHECK(ack == 0x01);
    CHECK(chip.bootBytes == 2048);
    CHECK(chip.packets.empty());
}

TEST_CASE("boot reports silence, no firmware and a wrong size")
{
    std::uint8_t ack = 0;
    {
        FakeSct3258 chip;
        chip.answerBoot = false;
        Sct3258 sct(chip, noDelay);
        CHECK(sct.boot(Blob, ack, 20) == Sct3258::BootResult::NoAnswer);
    }
    {
        FakeSct3258 chip;
        chip.bootAck = 0x00;
        Sct3258 sct(chip, noDelay);
        CHECK(sct.boot(Blob, ack) == Sct3258::BootResult::NoFirmware);
    }
    {
        FakeSct3258 chip;
        Sct3258 sct(chip, noDelay);
        CHECK(sct.boot(std::vector<std::uint8_t>(100), ack) == Sct3258::BootResult::BadBlob);
        CHECK(chip.bootBytes == 0);
    }
}

namespace {

// A chip whose status line is stuck low and returns noise.
class StuckBus final : public dmm::hal::HostPortBus
{
public:
    int reads = 0;
    void write(const std::uint8_t *, std::size_t) override {}
    std::uint8_t read() override
    {
        ++reads;
        return 0x5A;
    }
    bool dataWaiting() override { return true; }
};

} // namespace

TEST_CASE("a stuck status line cannot hold a request forever")
{
    StuckBus bus;
    Sct3258 sct(bus, noDelay);
    Packet p;
    const std::uint8_t q[] = {Sct3258::QueryHardware};
    CHECK_FALSE(sct.request(Sct3258::TypeControl, q, p, 5));
    CHECK(bus.reads < 5000);
}

TEST_CASE("queries, control fields and codec writes; packets are padded to 16 bits")
{
    FakeSct3258 chip;
    Sct3258 sct(chip, noDelay);
    std::uint8_t ack = 0;
    REQUIRE(sct.boot(Blob, ack) == Sct3258::BootResult::Ready);

    char text[16];
    REQUIRE(sct.queryText(Sct3258::QuerySoftware, text, sizeof text));
    CHECK(std::strcmp(text, "V2.01.07") == 0);
    REQUIRE(sct.queryText(Sct3258::QueryProduct, text, 5));
    CHECK(std::strcmp(text, "CT32") == 0);

    const std::uint8_t codec[] = {Sct3258::CodecSelect, 0x01};
    CHECK(sct.control(codec) == 0);
    const std::uint8_t unknown[] = {0x7E, 0x00};
    CHECK(sct.control(unknown) == 0x104);
    CHECK(sct.codecWrite(1, 0x10, 0x3A));

    CHECK_FALSE(chip.oddWriteSeen);
    REQUIRE(chip.packets.size() == 5);
    CHECK(chip.packets[0] == std::vector<std::uint8_t>{0x20, 0x32});
    CHECK(chip.packets[2] == std::vector<std::uint8_t>{0x00, 0x2D, 0x01});
    CHECK(chip.packets[4] == std::vector<std::uint8_t>{0x40, 0x00, 0x01, 0x10, 0x3A, 0x00, 0x00});
}

TEST_CASE("a status line stuck at data waiting still lets the caller yield and time out")
{
    class StuckBus final : public dmm::hal::HostPortBus
    {
    public:
        void write(const std::uint8_t *, std::size_t) override {}
        std::uint8_t read() override { return 0xFF; }
        bool dataWaiting() override { return true; }
    } bus;
    static unsigned delayedUs = 0;
    delayedUs = 0;
    Sct3258 sct(bus, [](unsigned us) { delayedUs += us; });
    Packet p;
    CHECK_FALSE(sct.receive(p, 5));
    CHECK(delayedUs >= 5000);
    std::uint8_t ack = 0;
    delayedUs = 0;
    CHECK(sct.boot(Blob, ack, 10) == Sct3258::BootResult::NoAnswer);
    CHECK(delayedUs >= 10000);
}

TEST_CASE("reports from the chip survive a request and never pass for its reply")
{
    FakeSct3258 chip;
    Sct3258 sct(chip, noDelay);
    std::uint8_t ack = 0;
    REQUIRE(sct.boot(std::vector<std::uint8_t>(2048, 0x11), ack) == Sct3258::BootResult::Ready);
    chip.queue(0x30, {0x7F, 0x87});                  // waiting before the request
    chip.reportsBeforeReply = {{0x30, {0x1B, 0x05}}, // a far-end event
        {0x20, {0x17, 0x0A}}};                       // everything sent
    const std::uint8_t cc[] = {Sct3258::DmrColourCode, 0x01};
    CHECK(sct.control(cc) == 0);

    Packet r;
    REQUIRE(sct.nextReport(r));
    CHECK(r.type == 0x30);
    CHECK(r.fields[0] == 0x7F);
    REQUIRE(sct.nextReport(r));
    CHECK(r.fields[0] == 0x1B);
    REQUIRE(sct.nextReport(r));
    CHECK(r.field(0x17)[0] == 0x0A);
    CHECK_FALSE(sct.nextReport(r));

    chip.queue(0x30, {0x77, 0x01, 0x43, 0x01, 0x09, 0, 0, 0, 0, 0, 1, 0, 0, 1}); // arrives later
    REQUIRE(sct.nextReport(r));
    CHECK(r.length == 14);
}
