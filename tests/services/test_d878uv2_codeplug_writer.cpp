// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "../drivers/fake_spi_nand.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_storage.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using dmm::drivers::storage::SpiNand;
using dmm::services::D878Uv2Codeplug;
using dmm::services::D878Uv2CodeplugWriter;
using dmm::services::D878Uv2Storage;
using dmm::services::readable;

namespace {

void noDelay(unsigned) {}

constexpr std::uint32_t Block = 0x02500000;
constexpr std::uint32_t BlockBytes = 0x20000;

// Old codeplug byte at an address: a pattern from the address.
std::uint8_t oldByte(std::uint32_t address)
{
    return static_cast<std::uint8_t>(address * 7 + 3);
}

// A flash whose block pair at @p block holds the old pattern in its first
// copy, marked valid, and a superseded second copy.
struct Radio
{
    FakeSpiNand chip;
    SpiNand nand{chip, noDelay};
    D878Uv2Storage storage{nand};
    D878Uv2Codeplug codeplug{storage};
    D878Uv2CodeplugWriter writer{storage, codeplug};

    explicit Radio(std::uint32_t block = Block)
    {
        std::vector<std::uint8_t> bytes(BlockBytes - 8);
        for (std::uint32_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = oldByte(block + i);
        }
        chip.put(block, bytes);
        chip.put(block + BlockBytes - 8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
        chip.put(block + 2 * BlockBytes - 8, {0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA});
        storage.start();
    }

    std::vector<std::uint8_t> read(std::uint32_t address, std::size_t n)
    {
        std::vector<std::uint8_t> out(n);
        REQUIRE(readable(codeplug.read(address, out)));
        return out;
    }
};

std::vector<std::uint8_t> filled(std::size_t n, std::uint8_t value)
{
    return std::vector<std::uint8_t>(n, value);
}

std::vector<std::uint8_t> oldBytes(std::uint32_t address, std::size_t n)
{
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = oldByte(address + static_cast<std::uint32_t>(i));
    }
    return out;
}

} // namespace

TEST_CASE("codeplug writer: new data on top of the old, in the block's other copy")
{
    Radio r;
    CHECK(r.writer.write(Block + 0x100, filled(16, 0x11)));
    CHECK(r.writer.write(Block + 0x1010, filled(16, 0x22)));
    CHECK(r.writer.write(Block + 0x10000, filled(40, 0x33)));
    CHECK(r.writer.wrote());
    CHECK(r.writer.finish());

    CHECK(r.read(Block + 0x100, 16) == filled(16, 0x11));
    CHECK(r.read(Block + 0x1010, 16) == filled(16, 0x22));
    CHECK(r.read(Block + 0x10000, 40) == filled(40, 0x33));
    CHECK(r.read(Block, 0x100) == oldBytes(Block, 0x100));
    CHECK(r.read(Block + 0x1020, 0x200) == oldBytes(Block + 0x1020, 0x200));
    CHECK(r.read(Block + 0x1F000, 0xFF8) == oldBytes(Block + 0x1F000, 0xFF8));
    CHECK(r.chip.erases == 1);
    // The second copy is now valid, the first superseded.
    std::array<std::uint8_t, 8> m{};
    REQUIRE(readable(r.storage.read(Block + 2 * BlockBytes - 8, m)));
    CHECK(m == std::array<std::uint8_t, 8>{0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    REQUIRE(readable(r.storage.read(Block + BlockBytes - 8, m)));
    CHECK(m == std::array<std::uint8_t, 8>{0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA});
    // Whole pages programmed once: the new copy reads clean.
    std::vector<std::uint8_t> page(2048);
    CHECK(r.storage.read(Block + BlockBytes + 0x800, page) == dmm::services::StorageResult::Ok);

    // A second write goes back to the first copy.
    CHECK(r.writer.write(Block + 0x100, filled(16, 0x44)));
    CHECK(r.writer.finish());
    CHECK(r.read(Block + 0x100, 16) == filled(16, 0x44));
    CHECK(r.read(Block + 0x1010, 16) == filled(16, 0x22));
    CHECK(r.read(Block + 0x200, 16) == oldBytes(Block + 0x200, 16));
}

TEST_CASE("codeplug writer: an unfinished write leaves the old codeplug")
{
    Radio r;
    CHECK(r.writer.write(Block + 0x100, filled(16, 0x11)));
    CHECK(r.writer.write(Block + 0x9000, filled(16, 0x11)));
    // Power lost here: a fresh reader sees the old data.
    D878Uv2Codeplug fresh(r.storage);
    std::vector<std::uint8_t> out(16);
    REQUIRE(readable(fresh.read(Block + 0x100, out)));
    CHECK(out == oldBytes(Block + 0x100, 16));
}

TEST_CASE("codeplug writer: a write into the next block finishes the one before")
{
    Radio r;
    CHECK(r.writer.write(Block + 0x100, filled(16, 0x11)));
    CHECK(r.writer.write(Block + 2 * BlockBytes + 0x40, filled(16, 0x55)));
    CHECK(r.read(Block + 0x100, 16) == filled(16, 0x11));
    CHECK(r.writer.finish());
    CHECK(r.read(Block + 2 * BlockBytes + 0x40, 16) == filled(16, 0x55));
    CHECK(r.chip.erases == 2);
}

TEST_CASE("codeplug writer: only the codeplug's own blocks, in rising order")
{
    Radio r;
    // The calibration, the radio data, below the codeplug, a second copy,
    // the markers.
    CHECK_FALSE(r.writer.write(0x02F60000, filled(16, 0)));
    CHECK_FALSE(r.writer.write(0x02FA0000, filled(16, 0)));
    CHECK_FALSE(r.writer.write(0x00000000, filled(16, 0)));
    CHECK_FALSE(r.writer.write(Block + BlockBytes, filled(16, 0)));
    CHECK_FALSE(r.writer.write(Block + BlockBytes - 8, filled(8, 0)));
    CHECK_FALSE(r.writer.write(Block + BlockBytes - 16, filled(16, 0)));
    // Past the contact ID table and callsign limits.
    CHECK_FALSE(r.writer.write(0x04880000, filled(16, 0)));
    CHECK(r.chip.erases == 0);
    CHECK(r.writer.write(Block + 0x5000, filled(16, 0x11)));
    CHECK_FALSE(r.writer.write(Block + 0x100, filled(16, 0x22)));
    CHECK(r.writer.finish());
    CHECK(r.read(Block + 0x100, 16) == oldBytes(Block + 0x100, 16));
}

TEST_CASE("codeplug writer: a flash failure stops it, the old codeplug stays")
{
    Radio r;
    // The erase of the second copy fails.
    r.chip.failErases[(Block + BlockBytes) / 2048] = true;
    CHECK_FALSE(r.writer.write(Block + 0x100, filled(16, 0x11)));
    CHECK_FALSE(r.writer.write(Block + 0x200, filled(16, 0x11)));
    CHECK_FALSE(r.writer.finish());
    CHECK(r.read(Block + 0x100, 16) == oldBytes(Block + 0x100, 16));
}

TEST_CASE("codeplug writer: the contact ID table is written like the rest")
{
    Radio r(0x04800000);
    CHECK(r.writer.write(0x04800010, filled(16, 0x77)));
    CHECK(r.writer.finish());
    CHECK(r.read(0x04800010, 16) == filled(16, 0x77));
    CHECK(r.read(0x04800000, 16) == oldBytes(0x04800000, 16));
}
