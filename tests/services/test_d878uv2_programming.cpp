// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "../drivers/fake_spi_nand.h"
#include "dmm/services/d878uv2_programming.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using dmm::drivers::storage::SpiNand;
using dmm::services::D878Uv2Codeplug;
using dmm::services::D878Uv2CodeplugWriter;
using dmm::services::D878Uv2Programming;
using dmm::services::D878Uv2Storage;
using Bytes = std::vector<std::uint8_t>;

namespace {

void noDelay(unsigned) {}

constexpr std::uint32_t Block = 0x02500000;

// A radio whose settings block holds byte (address + 1) at each address,
// in its first copy, marked valid.
struct Session
{
    FakeSpiNand chip;
    SpiNand nand{chip, noDelay};
    D878Uv2Storage storage{nand};
    D878Uv2Codeplug codeplug{storage};
    D878Uv2CodeplugWriter writer{storage, codeplug};
    D878Uv2Programming p{codeplug, writer};
    Bytes out;
    std::uint32_t now = 0;

    Session()
    {
        Bytes bytes(0x20000 - 8);
        for (std::uint32_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::uint8_t>(Block + i + 1);
        }
        chip.put(Block, bytes);
        chip.put(Block + 0x20000 - 8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
        storage.start();
    }

    Bytes send(const Bytes &in)
    {
        out.clear();
        p.feed(in, now, [this](std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); });
        return out;
    }
    Bytes send(const std::string &in) { return send(Bytes(in.begin(), in.end())); }

    Bytes read(std::uint32_t address)
    {
        const Bytes r = send(Bytes{'R', static_cast<std::uint8_t>(address >> 24), static_cast<std::uint8_t>(address >> 16),
            static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), 0x10});
        return r.size() == 24 ? Bytes(r.begin() + 6, r.begin() + 22) : Bytes{};
    }
};

Bytes writeCommand(std::uint32_t address, std::uint8_t fill)
{
    Bytes w{'W', static_cast<std::uint8_t>(address >> 24), static_cast<std::uint8_t>(address >> 16),
        static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), 0x10};
    w.resize(22, fill);
    std::uint8_t sum = 0;
    for (std::size_t i = 1; i < 22; ++i) {
        sum = static_cast<std::uint8_t>(sum + w[i]);
    }
    w.push_back(sum);
    w.push_back(0x06);
    return w;
}

} // namespace

TEST_CASE("programming: enter, identify, read, leave")
{
    Session s;
    CHECK(s.send(Bytes{0xDD, 'i', 'P', 'R'}).empty());
    CHECK_FALSE(s.p.active(s.now));
    CHECK(s.send("OGRAM") == Bytes{'Q', 'X', 0x06});
    CHECK(s.p.active(s.now));

    const Bytes id = s.send(Bytes{0x02});
    REQUIRE(id.size() == 16);
    CHECK(std::string(id.begin() + 1, id.begin() + 8) == "D878UV2");
    CHECK(id.front() == 'I');
    CHECK(id.back() == 0x06);

    // A read, its command split over two pieces.
    CHECK(s.send(Bytes{'R', 0x02, 0x50}).empty());
    const Bytes r = s.send(Bytes{0x10, 0x00, 0x10});
    REQUIRE(r.size() == 24);
    CHECK(r[0] == 'W');
    CHECK(Bytes(r.begin() + 1, r.begin() + 6) == Bytes{0x02, 0x50, 0x10, 0x00, 0x10});
    std::uint8_t sum = 0;
    for (std::size_t i = 1; i < 22; ++i) {
        CHECK((i < 6 || r[i] == static_cast<std::uint8_t>(0x02501000 + (i - 6) + 1)));
        sum = static_cast<std::uint8_t>(sum + r[i]);
    }
    CHECK(r[22] == sum);
    CHECK(r[23] == 0x06);

    CHECK(s.send("END") == Bytes{0x06});
    CHECK_FALSE(s.p.active(s.now));
    CHECK_FALSE(s.p.takeCodeplugChanged());
}

TEST_CASE("programming: writes are kept, END completes them and asks for a restart")
{
    Session s;
    s.send("PROGRAM");
    CHECK(s.send(writeCommand(Block + 0x100, 0x5A)) == Bytes{0x06});
    CHECK(s.send(writeCommand(Block + 0x110, 0x6B)) == Bytes{0x06});
    // A bad sum and the calibration are refused.
    Bytes bad = writeCommand(Block + 0x120, 0x11);
    bad[22] ^= 1;
    CHECK(s.send(bad) == Bytes{0x15});
    CHECK(s.send(writeCommand(0x02F60000, 0x11)) == Bytes{0x15});
    CHECK(s.send("END") == Bytes{0x06});
    CHECK(s.p.takeCodeplugChanged());
    CHECK_FALSE(s.p.takeCodeplugChanged());

    s.send("PROGRAM");
    CHECK(s.read(Block + 0x100) == Bytes(16, 0x5A));
    CHECK(s.read(Block + 0x110) == Bytes(16, 0x6B));
    CHECK(s.read(Block + 0x120)[0] == static_cast<std::uint8_t>(Block + 0x120 + 1));
}

TEST_CASE("programming: when the PC goes quiet mid-write, the old codeplug stays")
{
    Session s;
    s.send("PROGRAM");
    CHECK(s.send(writeCommand(Block + 0x100, 0x5A)) == Bytes{0x06});
    s.now += D878Uv2Programming::IdleMs;
    CHECK_FALSE(s.p.active(s.now));
    CHECK(s.p.takeCodeplugChanged());
    s.send("PROGRAM");
    CHECK(s.read(Block + 0x100)[0] == static_cast<std::uint8_t>(Block + 0x100 + 1));
    // Writing works again from the start of the block.
    CHECK(s.send(writeCommand(Block + 0x40, 0x22)) == Bytes{0x06});
    CHECK(s.send("END") == Bytes{0x06});
    s.send("PROGRAM");
    CHECK(s.read(Block + 0x40) == Bytes(16, 0x22));
}

TEST_CASE("programming: failed reads are not answered; a half command is dropped after a pause")
{
    Session s;
    s.send("PROGRAM");
    CHECK(s.send(Bytes{'R', 0x7F, 0x00, 0x00, 0x00, 0x10}).empty());
    s.send(Bytes{'R', 0x02});
    s.now += D878Uv2Programming::CommandTimeoutMs;
    CHECK(s.send(Bytes{0x02}).size() == 16);
}
