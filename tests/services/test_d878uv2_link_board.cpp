// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/ax25.h"
#include "dmm/services/d878uv2_link_board.h"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

using namespace dmm::services;
using Result = D878Uv2LinkBoardReader::Result;

namespace {

std::vector<std::uint8_t> encodedFrame(std::string_view info)
{
    ax25::Frame f;
    f.source = *ax25::parseAddress("N0CALL-7");
    f.destination = *ax25::parseAddress("APZDMM");
    f.path.pushBack(*ax25::parseAddress("WIDE1-1"));
    f.info << info;
    std::array<std::uint8_t, ax25::MaxFrameBytes> bytes{};
    const std::size_t n = ax25::encode(f, bytes);
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)};
}

struct Fed
{
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<std::string> lines;
};

Fed feedAll(D878Uv2LinkBoardReader &r, const std::vector<std::uint8_t> &bytes)
{
    Fed out;
    for (std::uint8_t b : bytes) {
        switch (r.feed(b)) {
        case Result::Frame:
            out.frames.emplace_back(r.frame().begin(), r.frame().end());
            break;
        case Result::Line:
            out.lines.emplace_back(r.line());
            break;
        case Result::None:
            break;
        }
    }
    return out;
}

void append(std::vector<std::uint8_t> &to, std::string_view text)
{
    to.insert(to.end(), text.begin(), text.end());
}

} // namespace

TEST_CASE("link board: the start answer, then packets between flags")
{
    const auto first = encodedFrame("!6010.25N/02456.30E>Test");
    const auto second = encodedFrame(">status ~ with flags ~~ inside");
    std::vector<std::uint8_t> stream{0x06};
    append(stream, "+READY\r\n+APRS:~");
    stream.insert(stream.end(), first.begin(), first.end());
    append(stream, "~+APRS:~");
    stream.insert(stream.end(), second.begin(), second.end());
    stream.push_back(0x7E);

    D878Uv2LinkBoardReader r;
    const Fed fed = feedAll(r, stream);
    REQUIRE(fed.lines.size() == 1);
    CHECK(fed.lines[0] == "+READY");
    REQUIRE(fed.frames.size() == 2);
    CHECK(fed.frames[0] == first);
    CHECK(fed.frames[1] == second);
    ax25::Frame f;
    REQUIRE(ax25::decode(fed.frames[1], f));
    CHECK(f.info.view() == ">status ~ with flags ~~ inside");
}

TEST_CASE("link board: a damaged frame is dropped and the next one still read")
{
    auto bad = encodedFrame("!6010.25N/02456.30E>Test");
    bad[20] ^= 0x40;
    const auto good = encodedFrame(">ok");
    std::vector<std::uint8_t> stream;
    append(stream, "+APRS:~");
    stream.insert(stream.end(), bad.begin(), bad.end());
    stream.push_back(0x7E);

    D878Uv2LinkBoardReader r;
    CHECK(feedAll(r, stream).frames.empty());
    // The board gives no further sign; a pause in the data resets the reader.
    r.reset();
    std::vector<std::uint8_t> next;
    append(next, "+APRS:~");
    next.insert(next.end(), good.begin(), good.end());
    next.push_back(0x7E);
    const Fed fed = feedAll(r, next);
    REQUIRE(fed.frames.size() == 1);
    CHECK(fed.frames[0] == good);
}
