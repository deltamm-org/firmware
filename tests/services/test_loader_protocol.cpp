// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "boot_test_rig.h"

#include "dmm/services/loader_protocol.h"

#include <doctest/doctest.h>

#include <string>

using namespace boot_test;

namespace {

void put32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::uint32_t get32(const std::vector<std::uint8_t> &b, std::size_t at)
{
    return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<std::uint32_t>(b[at + 3]) << 24;
}

struct Reply
{
    std::vector<std::uint8_t> payload;
    int status = -1;
};

struct Session
{
    Rig rig;
    Loader loader{rig.guarded, rig.layout, rig.buffer, rig.events};
    LoaderProtocol protocol{loader, "D878UV2", "0.25.0"};
    std::vector<std::uint8_t> out;
    std::uint32_t now = 0;

    explicit Session(bool withOldImage = true)
    {
        if (withOldImage) {
            rig.putInSlot(false, makeImage(Small, 2 * SectorBytes, 1, "old"));
        }
        loader.boot({});
    }

    void send(const std::vector<std::uint8_t> &bytes)
    {
        protocol.feed(bytes, now, [this](std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); });
    }

    // Takes one framed reply off the output; checks its CRC.
    Reply reply(std::uint8_t op)
    {
        Reply r;
        REQUIRE(out.size() >= 9);
        CHECK(out[0] == 0xDD);
        CHECK(out[1] == op);
        const std::size_t length = out[2] | out[3] << 8;
        REQUIRE(out.size() >= 4 + length + 5);
        const std::uint32_t crc = dmm::lib::crc32(out.data(), 4 + length + 1);
        CHECK(crc == get32(out, 4 + length + 1));
        r.payload.assign(out.begin() + 4, out.begin() + 4 + static_cast<std::ptrdiff_t>(length));
        r.status = out[4 + length];
        out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(4 + length + 5));
        return r;
    }

    Reply request(std::uint8_t op, const std::vector<std::uint8_t> &args)
    {
        std::vector<std::uint8_t> b{0xDD, op};
        b.insert(b.end(), args.begin(), args.end());
        send(b);
        return reply(op);
    }

    Reply erase(std::uint32_t length)
    {
        std::vector<std::uint8_t> a;
        put32(a, length);
        return request('e', a);
    }

    Reply write(std::uint32_t offset, const std::vector<std::uint8_t> &data, int crcFlip = 0)
    {
        std::vector<std::uint8_t> a;
        put32(a, offset);
        a.push_back(static_cast<std::uint8_t>(data.size()));
        a.push_back(static_cast<std::uint8_t>(data.size() >> 8));
        a.insert(a.end(), data.begin(), data.end());
        put32(a, dmm::lib::crc32(data.data(), data.size()) ^ static_cast<std::uint32_t>(crcFlip));
        return request('w', a);
    }

    Reply checksum(std::uint32_t offset, std::uint32_t length)
    {
        std::vector<std::uint8_t> a;
        put32(a, offset);
        put32(a, length);
        return request('k', a);
    }

    Reply commit(std::uint32_t length, std::uint32_t crc, std::uint8_t mode = 0)
    {
        std::vector<std::uint8_t> a;
        put32(a, length);
        put32(a, crc);
        a.push_back(mode);
        return request('c', a);
    }

    // Sends @p image in 4 KB writes.
    void download(const std::vector<std::uint8_t> &image)
    {
        REQUIRE(erase(static_cast<std::uint32_t>(image.size())).status == 0);
        for (std::size_t at = 0; at < image.size(); at += 4096) {
            const std::size_t n = std::min<std::size_t>(4096, image.size() - at);
            const std::vector<std::uint8_t> chunk(image.begin() + static_cast<std::ptrdiff_t>(at),
                image.begin() + static_cast<std::ptrdiff_t>(at + n));
            REQUIRE(write(static_cast<std::uint32_t>(at), chunk).status == 0);
        }
    }
};

constexpr int Ok = 0;
constexpr int BadCrc = 1;
constexpr int OutOfRange = 2;
constexpr int FlashError = 3;
constexpr int Refused = 5;
constexpr int ImageRejected = 6;
constexpr int BadRequest = 0xFE;

} // namespace

TEST_CASE("loader protocol: information names the loader, the model and both slots")
{
    Session s;
    s.protocol.setStayReason(StayReason::Requested);
    const Reply r = s.request('i', {});
    CHECK(r.status == Ok);
    const auto &p = r.payload;
    REQUIRE(p.size() > 40);
    CHECK(std::string(p.begin(), p.begin() + 4) == "DMLD");
    CHECK(p[4] == LoaderProtocol::Version);
    CHECK(p[5] == 7);
    CHECK(std::string(p.begin() + 6, p.begin() + 13) == "D878UV2");
    std::size_t at = 13;
    CHECK(std::string(p.begin() + static_cast<std::ptrdiff_t>(at) + 1, p.begin() + static_cast<std::ptrdiff_t>(at) + 1 + p[at]) == "0.25.0");
    at += 1 + p[at];
    CHECK(get32(p, at) == Small.slotA.begin);
    CHECK(get32(p, at + 8) == Small.slotB.begin);
    at += 16;
    CHECK(p[at] == static_cast<std::uint8_t>(SlotState::Confirmed));
    CHECK(get32(p, at + 2) == 2 * SectorBytes);
    at += 10;
    CHECK(std::string(p.begin() + static_cast<std::ptrdiff_t>(at) + 1, p.begin() + static_cast<std::ptrdiff_t>(at) + 1 + p[at]) == "old");
    at += 1 + p[at];
    CHECK(p[at] == static_cast<std::uint8_t>(SlotState::Empty));
    at += 10;
    CHECK(p[at] == 0);
    at += 1;
    CHECK(p[at + 1] == Loader::MaxAttempts);
    CHECK(p[at + 4] == static_cast<std::uint8_t>(StayReason::Requested));
    CHECK(at + 5 == p.size());
}

TEST_CASE("loader protocol: download, check, commit and restart")
{
    Session s;
    const auto image = makeImage(Small, 3 * SectorBytes + 40, 7, "new");
    s.send({0x7E, 0x00, 'A', 'T', '\r'}); // junk such as a modem probe
    s.download(image);
    const Reply k = s.checksum(0, static_cast<std::uint32_t>(image.size()));
    CHECK(k.status == Ok);
    CHECK(get32(k.payload, 0) == crcOf(image));
    CHECK(s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image), 1).status == Ok);
    CHECK(s.rig.state().b.state == SlotState::Committed);
    CHECK(s.rig.state().b.mode == ConfirmMode::Manual);
    // Writes after the commit are refused.
    CHECK(s.write(0, {1, 2, 3, 4}).status == Refused);
    const Reply b = s.request('b', {0, 0, 0});
    CHECK(b.status == Ok);
    CHECK(s.protocol.restart().wanted);
    CHECK(s.protocol.restart().request == Request::None);
    CHECK(s.rig.settle().outcome == Outcome::StartImage);
    CHECK(s.rig.slotHolds(false, image));
}

TEST_CASE("loader protocol: bad data, ranges and lengths are refused")
{
    Session s;
    REQUIRE(s.erase(0).status == Ok);
    CHECK(s.write(0, {1, 2, 3, 4}, 1).status == BadCrc);
    CHECK(s.write(2, {1, 2, 3, 4}).status == OutOfRange);
    CHECK(s.write(0, {1, 2, 3}).status == OutOfRange);
    const std::uint32_t capacity = Small.slotB.end - Small.slotB.begin;
    CHECK(s.write(capacity - 4, {1, 2, 3, 4}).status == Ok);
    CHECK(s.write(capacity, {1, 2, 3, 4}).status == OutOfRange);
    CHECK(s.write(capacity - 4, {1, 2, 3, 4, 5, 6, 7, 8}).status == OutOfRange);
    // A second write over programmed flash fails without harm.
    CHECK(s.write(capacity - 4, {9, 9, 9, 9}).status == FlashError);
    CHECK(s.erase(capacity + 1).status == OutOfRange);
    CHECK(s.checksum(capacity, 1).status == OutOfRange);
    CHECK(s.checksum(capacity, 0).status == Ok);
    // Too long a write is refused from its header on.
    s.send({0xDD, 'w', 0, 0, 0, 0, 0x01, 0x10});
    CHECK(s.reply('w').status == BadRequest);
    // Unknown letters.
    CHECK(s.request('Z', {}).status == BadRequest);
    CHECK(s.request('b', {7, 0, 0}).status == BadRequest);
    CHECK(s.commit(16, 0, 9).status == BadRequest);
    CHECK(s.rig.untouched());
}

TEST_CASE("loader protocol: commit checks the image")
{
    Session s;
    auto image = makeImage(Small, 2 * SectorBytes, 9);
    SUBCASE("wrong CRC")
    {
        s.download(image);
        const Reply r = s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image) ^ 1);
        CHECK(r.status == ImageRejected);
        CHECK(r.payload.at(0) == static_cast<std::uint8_t>(ImageProblem::BadCrc));
    }
    SUBCASE("linked for another address")
    {
        const BootLayout moved{{Small.slotA.begin + SectorBytes, Small.slotA.end}, Small.slotB, {Small.record[0], Small.record[1]}, Small.ram, Small.infoOffset};
        image = makeImage(moved, 2 * SectorBytes, 9);
        s.download(image);
        const Reply r = s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image));
        CHECK(r.status == ImageRejected);
        CHECK(r.payload.at(0) == static_cast<std::uint8_t>(ImageProblem::WrongBase));
    }
    SUBCASE("stack pointer outside RAM")
    {
        image[3] = 0x30;
        s.download(image);
        const Reply r = s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image));
        CHECK(r.payload.at(0) == static_cast<std::uint8_t>(ImageProblem::BadStack));
    }
    SUBCASE("length the info block does not name")
    {
        s.download(image);
        std::uint32_t crc = 0;
        REQUIRE(flashCrc(s.rig.guarded, Small.slotB.begin, 2 * SectorBytes - 4, crc));
        const Reply r = s.commit(2 * SectorBytes - 4, crc);
        CHECK(r.payload.at(0) == static_cast<std::uint8_t>(ImageProblem::WrongLength));
    }
    SUBCASE("no info block")
    {
        image[Small.infoOffset] = 0;
        s.download(image);
        const Reply r = s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image));
        CHECK(r.payload.at(0) == static_cast<std::uint8_t>(ImageProblem::NoInfo));
    }
    CHECK(s.rig.state().b.state == SlotState::Empty);
}

TEST_CASE("loader protocol: slot B is not erased while it holds the only confirmed image")
{
    Session s;
    const auto image = makeImage(Small, 2 * SectorBytes + 8, 3, "new");
    s.download(image);
    REQUIRE(s.commit(static_cast<std::uint32_t>(image.size()), crcOf(image), 1).status == Ok);
    REQUIRE(s.rig.settle().outcome == Outcome::StartImage);
    // The new image is on trial; the old one in B is the only confirmed one.
    Session &again = s;
    again.loader.record().load();
    CHECK(again.erase(0).status == Refused);
    CHECK(again.write(0, {1, 2, 3, 4}).status == Refused);
    REQUIRE(s.rig.confirm());
    again.loader.record().load();
    CHECK(again.erase(0).status == Ok);
}

TEST_CASE("loader protocol: a request cut short is dropped after a second")
{
    Session s;
    s.send({0xDD, 'e', 0});
    CHECK(s.out.empty());
    s.now = 1500;
    s.send({0xDD, 'i'});
    CHECK(s.reply('i').status == Ok);
}

TEST_CASE("loader protocol: roll back and test cut restarts")
{
    Session s;
    CHECK(s.request('b', {1, 0, 0}).status == Ok);
    CHECK(s.protocol.restart().request == Request::Rollback);
    CHECK(s.request('b', {2, 7, 0}).status == Ok);
    CHECK(s.protocol.restart().swapCut == 7);
}
