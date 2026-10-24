// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "../drivers/fake_spi_nand.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_storage.h"

#include <doctest/doctest.h>

#include <string_view>
#include <vector>

using namespace dmm;
using dmm::drivers::storage::SpiNand;
using dmm::services::D878Uv2Calibration;
using dmm::services::D878Uv2Storage;
using dmm::services::StorageResult;

namespace {

void noDelay(unsigned) {}

// A remap record in slot @p slot moving block @p from to block @p to.
void putRecord(FakeSpiNand &chip, int slot, std::vector<std::pair<std::uint16_t, std::uint16_t>> moves)
{
    std::vector<std::uint8_t> r(0x1F8, 0);
    for (std::size_t i = 0; i < moves.size(); ++i) {
        r[2 * i] = static_cast<std::uint8_t>(moves[i].first);
        r[2 * i + 1] = static_cast<std::uint8_t>(moves[i].first >> 8);
        r[0xFA + 2 * i] = static_cast<std::uint8_t>(moves[i].second);
        r[0xFA + 2 * i + 1] = static_cast<std::uint8_t>(moves[i].second >> 8);
    }
    r[0x1F4] = 0x55;
    r[0x1F5] = static_cast<std::uint8_t>(slot);
    r[0x1F6] = static_cast<std::uint8_t>(moves.size());
    chip.put(static_cast<std::uint32_t>(slot) * 0x400, r);
}

} // namespace

TEST_CASE("the newest remap record is used for logical reads")
{
    FakeSpiNand chip;
    putRecord(chip, 0, {{0x10, 0x180}});
    putRecord(chip, 1, {{0x10, 0x180}, {0x17B, 0x181}}); // 0x17B = the block of 0x02F60000
    chip.put(0x181 * 0x20000 + 5, {0xAB});
    chip.put(0x02F60000 + 5, {0x11});
    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();
    CHECK(storage.info().present);
    CHECK(storage.info().remappedBlocks == 2);
    CHECK(storage.info().sizeBytes == 256u * 1024 * 1024);
    CHECK(storage.remapBlock(0x17B) == 0x181);
    CHECK(storage.remapBlock(0x17C) == 0x17C);

    std::uint8_t b = 0;
    CHECK(storage.read(0x02F60005, std::span(&b, 1)) == StorageResult::Ok);
    CHECK(b == 0xAB);
    CHECK(storage.read(0x02F60005, std::span(&b, 1), true) == StorageResult::Ok);
    CHECK(b == 0x11);
}

TEST_CASE("reads cross pages and blocks; the worst result is reported")
{
    FakeSpiNand chip;
    putRecord(chip, 0, {{1, 3}});
    chip.put(0x1FFFF, {0x01});     // last byte of block 0
    chip.put(3 * 0x20000, {0x02}); // first byte of block 1 = block 3
    chip.eccStatus[3 * 64] = 0x10; // first page of block 3: corrected
    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();
    std::uint8_t two[2] = {};
    CHECK(storage.read(0x1FFFF, two) == StorageResult::Corrected);
    CHECK(two[0] == 0x01);
    CHECK(two[1] == 0x02);
    CHECK(storage.read(256u * 1024 * 1024 - 1, two) == StorageResult::OutOfRange);
}

TEST_CASE("no record, a broken count or no chip mean no remapping")
{
    {
        FakeSpiNand chip;
        SpiNand nand(chip, noDelay);
        D878Uv2Storage storage(nand);
        storage.start();
        CHECK(storage.info().remappedBlocks == -1);
    }
    {
        FakeSpiNand chip;
        putRecord(chip, 0, {});
        chip.put(0x1F6, {200});
        SpiNand nand(chip, noDelay);
        D878Uv2Storage storage(nand);
        storage.start();
        CHECK(storage.info().remappedBlocks == -1);
    }
    {
        FakeSpiNand chip;
        chip.maker = 0x00;
        SpiNand nand(chip, noDelay);
        D878Uv2Storage storage(nand);
        storage.start();
        CHECK_FALSE(storage.info().present);
        std::uint8_t b = 0;
        CHECK(storage.read(0, std::span(&b, 1)) == StorageResult::NoChip);
    }
}

TEST_CASE("calibration comes from the valid copy and gives the battery offset")
{
    FakeSpiNand chip;
    std::vector<std::uint8_t> copyA(0x620, 0), copyB(0x620, 0);
    copyA[0x11] = 112; // +120 mV
    copyA[0x14] = 'w';
    copyA[0x15] = 'w';
    copyB = copyA;
    copyB[0x11] = 90; // -100 mV
    chip.put(D878Uv2Calibration::CopyA, copyA);
    chip.put(D878Uv2Calibration::CopyB, copyB);
    chip.put(D878Uv2Calibration::CopyA + 0x1FFF8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();

    D878Uv2Calibration cal;
    REQUIRE(cal.load(storage));
    CHECK(cal.copy() == 0);
    CHECK(cal.present());
    CHECK(cal.batteryOffsetMv() == 120);

    // A superseded copy A: copy B is used.
    chip.put(D878Uv2Calibration::CopyA + 0x1FFF8, {0xAA, 0xAA, 0x55, 0x55});
    REQUIRE(cal.load(storage));
    CHECK(cal.copy() == 1);
    CHECK(std::string_view(cal.source()) == "copy B");
    CHECK(cal.batteryOffsetMv() == -100);

    // Never calibrated: no offset.
    chip.put(D878Uv2Calibration::CopyB + 0x14, {0, 0});
    REQUIRE(cal.load(storage));
    CHECK_FALSE(cal.present());
    CHECK_FALSE(cal.batteryOffsetMv().has_value());
}

TEST_CASE("calibration pages the flash flags as uncorrectable are still used")
{
    FakeSpiNand chip;
    std::vector<std::uint8_t> copyA(0x620, 0);
    copyA[0x11] = 105; // +50 mV
    copyA[0x14] = 'w';
    copyA[0x15] = 'w';
    chip.put(D878Uv2Calibration::CopyA, copyA);
    chip.put(D878Uv2Calibration::CopyA + 0x1FFF8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    chip.eccStatus[D878Uv2Calibration::CopyA / 2048] = 0x20;
    chip.eccStatus[(D878Uv2Calibration::CopyA + 0x1FFF8) / 2048] = 0x20;
    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();

    D878Uv2Calibration cal;
    REQUIRE(cal.load(storage));
    CHECK(cal.copy() == 0);
    CHECK(cal.flagged());
    CHECK(std::string_view(cal.source()).starts_with("copy A (flash check failed"));
    CHECK(cal.batteryOffsetMv() == 50);
}
