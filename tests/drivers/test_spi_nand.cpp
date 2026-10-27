// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/storage/spi_nand.h"
#include "fake_spi_nand.h"

#include <doctest/doctest.h>

#include <algorithm>

using dmm::drivers::storage::SpiNand;

namespace {

void noDelay(unsigned) {}

} // namespace

TEST_CASE("the Toshiba 2 Gbit parts are recognised; others are not")
{
    FakeSpiNand chip;
    SpiNand nand(chip, noDelay);
    CHECK(nand.identify());
    CHECK(nand.pageCount() == 131072);
    chip.device = 0xEB;
    CHECK(nand.identify());
    chip.maker = 0xC8;
    CHECK_FALSE(nand.identify());
    CHECK_FALSE(nand.identified());
}

TEST_CASE("reads load a page once, then read its cache from any column")
{
    FakeSpiNand chip;
    chip.busyPolls = 3;
    chip.put(5 * 2048 + 100, {1, 2, 3, 4});
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    std::uint8_t data[4] = {};
    CHECK(nand.read(5, 100, data, 4) == SpiNand::Result::Ok);
    CHECK(data[0] == 1);
    CHECK(data[3] == 4);
    CHECK(nand.read(5, 102, data, 1) == SpiNand::Result::Ok);
    CHECK(data[0] == 3);
    CHECK(chip.pageLoads == 1);
    CHECK(nand.read(6, 0, data, 1) == SpiNand::Result::Ok);
    CHECK(data[0] == 0xFF);
    CHECK(chip.pageLoads == 2);
}

TEST_CASE("ECC status and a chip that never gets ready are reported")
{
    FakeSpiNand chip;
    chip.eccStatus[7] = 0x10;
    chip.eccStatus[8] = 0x20;
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    std::uint8_t b = 0;
    CHECK(nand.read(7, 0, &b, 1) == SpiNand::Result::Corrected);
    CHECK(nand.read(8, 0, &b, 1) == SpiNand::Result::Uncorrectable);
    chip.busyPolls = 100000;
    CHECK(nand.read(9, 0, &b, 1) == SpiNand::Result::Timeout);
    // A timed-out page is loaded again next time.
    chip.busyPolls = 0;
    CHECK(nand.read(9, 0, &b, 1) == SpiNand::Result::Ok);
}

TEST_CASE("a page flagged uncorrectable is loaded again, a few times at most")
{
    FakeSpiNand chip;
    chip.flaggedLoads[3] = 2;
    chip.eccStatus[4] = 0x20;
    chip.put(4 * 2048, {0x42});
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    std::uint8_t b = 0;
    CHECK(nand.read(3, 0, &b, 1) == SpiNand::Result::Ok);
    CHECK(chip.pageLoads == 3);
    chip.pageLoads = 0;
    CHECK(nand.read(4, 0, &b, 1) == SpiNand::Result::Uncorrectable);
    CHECK(chip.pageLoads == static_cast<int>(SpiNand::LoadTries));
    CHECK(b == 0x42);
    // Still loaded: the next read of the page does not load it again.
    CHECK(nand.read(4, 0, &b, 1) == SpiNand::Result::Uncorrectable);
    CHECK(chip.pageLoads == static_cast<int>(SpiNand::LoadTries));
}

TEST_CASE("reading never sends a write, program or erase command")
{
    FakeSpiNand chip;
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    std::uint8_t buffer[3000];
    for (std::uint32_t page : {0u, 1u, 70000u}) {
        nand.read(page, 0, buffer, 2048);
    }
    const std::uint8_t allowed[] = {0x9F, 0x13, 0x0F, 0x3B};
    for (std::uint8_t op : chip.opcodes) {
        CHECK(std::find(std::begin(allowed), std::end(allowed), op) != std::end(allowed));
    }
}

TEST_CASE("erase and program need the blocks unlocked, and program clears bits only")
{
    FakeSpiNand chip;
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    std::uint8_t data[2048];
    std::fill(std::begin(data), std::end(data), 0x5A);
    // Locked, as the chip comes up: refused.
    CHECK(nand.program(64, 0, data, sizeof data) == SpiNand::Result::Failed);
    nand.unlock();
    CHECK(chip.blockLock == 0);
    CHECK(nand.program(64, 0, data, sizeof data) == SpiNand::Result::Ok);
    std::uint8_t back[2048] = {};
    CHECK(nand.read(64, 0, back, sizeof back) == SpiNand::Result::Ok);
    CHECK(std::equal(std::begin(data), std::end(data), std::begin(back)));
    // A second program of the same page clears more bits and breaks its ECC.
    const std::uint8_t more[] = {0x0F};
    CHECK(nand.program(64, 10, more, 1) == SpiNand::Result::Ok);
    CHECK(nand.read(64, 10, back, 1) == SpiNand::Result::Uncorrectable);
    CHECK(back[0] == (0x5A & 0x0F));
    // Erase works on the whole block holding the page.
    CHECK(nand.eraseBlock(100) == SpiNand::Result::Ok);
    CHECK(nand.read(64, 0, back, 4) == SpiNand::Result::Ok);
    CHECK(back[0] == 0xFF);
    CHECK(chip.erases == 1);
}

TEST_CASE("a failed erase or program is reported")
{
    FakeSpiNand chip;
    SpiNand nand(chip, noDelay);
    REQUIRE(nand.identify());
    nand.unlock();
    chip.failErases[128] = true;
    chip.failPrograms[200] = true;
    CHECK(nand.eraseBlock(130) == SpiNand::Result::Failed);
    const std::uint8_t one[] = {0x00};
    CHECK(nand.program(200, 0, one, 1) == SpiNand::Result::Failed);
    CHECK(nand.program(201, 0, one, 1) == SpiNand::Result::Ok);
}
