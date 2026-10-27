// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_programming.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using dmm::services::D878Uv2Programming;
using Bytes = std::vector<std::uint8_t>;

namespace {

// Byte i of the codeplug is the low byte of its address plus one.
bool fakeRead(std::uint32_t address, std::span<std::uint8_t> data)
{
    if (address >= 0x03000000) {
        return false;
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(address + i + 1);
    }
    return true;
}

struct Session
{
    D878Uv2Programming p{fakeRead};
    Bytes out;
    std::uint32_t now = 0;

    Bytes send(const Bytes &in)
    {
        out.clear();
        p.feed(in, now, [this](std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); });
        return out;
    }
    Bytes send(const std::string &in) { return send(Bytes(in.begin(), in.end())); }
};

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
}

TEST_CASE("programming: writes are refused, failed reads are not answered")
{
    Session s;
    s.send("PROGRAM");
    Bytes write{'W', 0x02, 0x50, 0x00, 0x00, 0x10};
    write.resize(24, 0x00);
    CHECK(s.send(write) == Bytes{0x15});
    CHECK(s.send(Bytes{'R', 0x03, 0x00, 0x00, 0x00, 0x10}).empty());
    // The next command is still understood.
    CHECK(s.send(Bytes{0x02}).size() == 16);
}

TEST_CASE("programming: program mode ends when the PC goes quiet")
{
    Session s;
    s.send("PROGRAM");
    s.now = D878Uv2Programming::IdleMs - 1;
    CHECK(s.p.active(s.now));
    s.send(Bytes{0x02});
    s.now += D878Uv2Programming::IdleMs;
    CHECK_FALSE(s.p.active(s.now));
    // Outside program mode a read is not answered.
    CHECK(s.send(Bytes{'R', 0x02, 0x50, 0x00, 0x00, 0x10}).empty());
}

TEST_CASE("programming: a half command is dropped after a pause")
{
    Session s;
    s.send("PROGRAM");
    s.send(Bytes{'R', 0x02});
    s.now += D878Uv2Programming::CommandTimeoutMs;
    CHECK(s.send(Bytes{0x02}).size() == 16);
}
