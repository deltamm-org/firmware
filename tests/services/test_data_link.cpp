// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/lib/crc32.h"
#include "dmm/services/data_link.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace dmm;
using namespace dmm::services;

namespace {

class MemoryStorage final : public Storage
{
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x10000);
    std::vector<std::pair<std::uint32_t, bool>> reads;
    StorageResult result = StorageResult::Ok;

    StorageInfo info() override { return {true, 0x98, 0xCB, 0x10000, 3}; }
    StorageResult read(std::uint32_t address, std::span<std::uint8_t> data, bool physical) override
    {
        reads.emplace_back(address, physical);
        if (address + data.size() > bytes.size()) {
            return StorageResult::OutOfRange;
        }
        std::copy_n(bytes.begin() + address, data.size(), data.begin());
        return result;
    }
};

struct Session
{
    MemoryStorage storage;
    DataLink link{&storage, "0.9.0"};
    std::vector<std::uint8_t> out;
    os::Ticks now = 0;

    void send(std::vector<std::uint8_t> request)
    {
        link.feed(request, now, [this](std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); });
    }

    // Splits the reply: checks framing and CRC, returns {payload, status}.
    std::pair<std::vector<std::uint8_t>, int> reply(std::uint8_t op)
    {
        REQUIRE(out.size() >= 9);
        CHECK(out[0] == 0xDD);
        CHECK(out[1] == op);
        const std::size_t length = out[2] | out[3] << 8;
        REQUIRE(out.size() == 4 + length + 1 + 4);
        const std::uint32_t crc = lib::crc32(out.data(), out.size() - 4);
        const std::uint32_t sent = out[out.size() - 4] | out[out.size() - 3] << 8 | out[out.size() - 2] << 16 |
            static_cast<std::uint32_t>(out[out.size() - 1]) << 24;
        CHECK(crc == sent);
        std::vector<std::uint8_t> payload(out.begin() + 4, out.begin() + 4 + static_cast<std::ptrdiff_t>(length));
        const int status = out[4 + length];
        out.clear();
        return {payload, status};
    }
};

} // namespace

TEST_CASE("crc32 matches the usual check value")
{
    const std::uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(lib::crc32(text, sizeof text) == 0xCBF43926u);
    CHECK(lib::crc32(text + 4, 5, lib::crc32(text, 4)) == 0xCBF43926u);
}

TEST_CASE("information reply")
{
    Session s;
    s.send({0x00, 0x41, 0xDD, 'i'}); // leading noise is skipped
    const auto [payload, status] = s.reply('i');
    CHECK(status == 0);
    REQUIRE(payload.size() == 15);
    CHECK(payload[0] == 1);
    CHECK(payload[1] == 0x98);
    CHECK(payload[2] == 0xCB);
    CHECK((payload[3] | payload[4] << 8 | payload[5] << 16) == 0x10000);
    CHECK((payload[7] | payload[8] << 8) == 3);
    CHECK(payload[9] == 5);
    CHECK(std::string(payload.begin() + 10, payload.end()) == "0.9.0");
}

TEST_CASE("reads, logical and physical, in pieces, arriving byte by byte")
{
    Session s;
    for (std::size_t i = 0; i < 1000; ++i) {
        s.storage.bytes[0x1000 + i] = static_cast<std::uint8_t>(i * 7);
    }
    for (std::uint8_t b : std::vector<std::uint8_t>{0xDD, 'r', 0x00, 0x10, 0x00, 0x00, 0xE8, 0x03}) {
        s.send({b});
    }
    auto [payload, status] = s.reply('r');
    CHECK(status == 0);
    REQUIRE(payload.size() == 1000);
    CHECK(payload[999] == static_cast<std::uint8_t>(999 * 7));
    CHECK(s.storage.reads.size() == 4); // 256-byte pieces
    CHECK_FALSE(s.storage.reads[0].second);

    s.storage.reads.clear();
    s.send({0xDD, 'p', 0x00, 0x10, 0x00, 0x00, 0x01, 0x00});
    std::tie(payload, status) = s.reply('p');
    CHECK(payload.size() == 1);
    CHECK(s.storage.reads.at(0).second);
}

TEST_CASE("status reports the worst piece; bad requests get 0xFE")
{
    Session s;
    s.storage.result = StorageResult::Corrected;
    s.send({0xDD, 'r', 0, 0, 0, 0, 16, 0});
    CHECK(s.reply('r').second == 1);
    s.send({0xDD, 'r', 0xF0, 0xFF, 0, 0, 0, 1}); // past the end
    auto [payload, status] = s.reply('r');
    CHECK(status == 5);
    CHECK(payload.front() == 0xFF);
    s.send({0xDD, 'r', 0, 0, 0, 0, 0x01, 0x10}); // 4097 bytes
    CHECK(s.reply('r').second == 0xFE);
    s.send({0xDD, 'W'});
    CHECK(s.reply('W').second == 0xFE);
}

TEST_CASE("without storage, reads answer no chip")
{
    DataLink link(nullptr, "x");
    std::vector<std::uint8_t> out;
    const std::vector<std::uint8_t> request{0xDD, 'r', 0, 0, 0, 0, 4, 0};
    link.feed(request, 0, [&](std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); });
    REQUIRE(out.size() == 4 + 4 + 1 + 4);
    CHECK(out[8] == 4);
}

TEST_CASE("an unfinished request is dropped after a second")
{
    Session s;
    s.send({0xDD, 'r', 1, 2});
    s.now = 1500;
    s.send({0xDD, 'i'});
    CHECK(s.reply('i').second == 0);
}

namespace {

class LoaderSystem final : public SystemControl
{
public:
    bool present = true;
    bool confirmed = false;

    bool powerSwitchOff() override { return false; }
    void holdPower() override {}
    void releasePower() override {}
    void feedWatchdog() override {}
    void restart() override {}
    ResetReport lastReset() override { return {}; }
    void testFault() override {}
    bool confirmImage() override
    {
        confirmed = present;
        return present;
    }
    LoaderStatus loaderStatus() override
    {
        LoaderStatus s;
        s.present = present;
        s.trial = !confirmed;
        s.manual = true;
        s.attempts = 2;
        s.maxAttempts = 3;
        s.previousKept = true;
        s.fallback = boot::Fallback::Requested;
        return s;
    }
};

} // namespace

TEST_CASE("loader requests: status in the information, restart and confirm")
{
    Session s;
    LoaderSystem system;
    DataLink link{&s.storage, "0.9.0", &system};
    auto send = [&](std::vector<std::uint8_t> r) {
        link.feed(r, 0, [&](std::span<const std::uint8_t> b) { s.out.insert(s.out.end(), b.begin(), b.end()); });
    };
    send({0xDD, 'i'});
    auto [payload, status] = s.reply('i');
    REQUIRE(payload.size() == 15 + 7);
    CHECK(std::vector<std::uint8_t>(payload.begin() + 15, payload.end()) == std::vector<std::uint8_t>{1, 1, 1, 2, 3, 1, 3});

    send({0xDD, 'B', 2});
    CHECK(s.reply('B').second == 0);
    CHECK(link.takeLoaderRequest() == LoaderRequest::Rollback);
    CHECK_FALSE(link.takeLoaderRequest());
    link.setBusy(true);
    send({0xDD, 'B', 1});
    CHECK(s.reply('B').second == DataLink::Refused);
    CHECK_FALSE(link.takeLoaderRequest());
    link.setBusy(false);
    send({0xDD, 'B', 9});
    CHECK(s.reply('B').second == DataLink::BadRequest);

    send({0xDD, 'K'});
    CHECK(s.reply('K').second == 0);
    CHECK(system.confirmed);

    system.present = false;
    send({0xDD, 'B', 1});
    CHECK(s.reply('B').second == DataLink::Refused);
    send({0xDD, 'K'});
    CHECK(s.reply('K').second == DataLink::Refused);

    // Without a SystemControl both are refused.
    send({0xDD, 'i'});
    s.reply('i');
    DataLink plain{&s.storage, "0.9.0"};
    plain.feed(std::vector<std::uint8_t>{0xDD, 'K'}, 0, [&](std::span<const std::uint8_t> b) { s.out.insert(s.out.end(), b.begin(), b.end()); });
    CHECK(s.reply('K').second == DataLink::Refused);
}
