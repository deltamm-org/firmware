// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/lib/format.h"
#include "dmm/lib/sha256.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

using namespace dmm::lib;

TEST_CASE("TextWriter formats text, integers, bools and hex")
{
    char buf[64];
    TextWriter w(buf);
    w << "x=" << 42 << ' ' << -7 << ' ' << true << ' ' << hex(0x2a, 4) << ' ' << std::uint64_t{1} << 40;
    CHECK(w.view() == "x=42 -7 true 002a 140");
    CHECK_FALSE(w.truncated());
}

TEST_CASE("TextWriter formats the ends of each integer range")
{
    char buf[128];
    TextWriter w(buf);
    w << 0 << ' ' << std::int8_t{-128} << ' ' << std::uint8_t{255} << ' ' << std::numeric_limits<std::int32_t>::min()
      << ' ' << std::numeric_limits<std::uint32_t>::max() << ' ' << std::numeric_limits<std::int64_t>::min() << ' '
      << std::numeric_limits<std::uint64_t>::max();
    CHECK(w.view() == "0 -128 255 -2147483648 4294967295 -9223372036854775808 18446744073709551615");
}

TEST_CASE("TextWriter pads decimals with zeros")
{
    char buf[32];
    TextWriter w(buf);
    w << zeroPadded(7, 2) << ':' << zeroPadded(123, 2) << ' ' << zeroPadded(42, 5) << ' ' << zeroPadded(0, 1);
    CHECK(w.view() == "07:123 00042 0");
}

TEST_CASE("TextWriter truncates and remembers it")
{
    char buf[4];
    TextWriter w(buf);
    w << "hello";
    CHECK(w.view() == "hell");
    CHECK(w.truncated());
}

TEST_CASE("StaticString appends across calls")
{
    StaticString<16> s("up ");
    s << 12 << " s";
    CHECK(s.view() == "up 12 s");
    s << " and more text";
    CHECK(s.size() == 16);
    CHECK(s.truncated());
}

namespace {

std::string hexOf(std::string_view text)
{
    Sha256 s;
    s.update(text);
    auto h = Sha256::toHex(s.finish());
    return {h.data(), h.size()};
}

} // namespace

TEST_CASE("sha256 matches the FIPS 180-4 examples")
{
    CHECK(hexOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hexOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 s;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        s.update(chunk);
    }
    auto h = Sha256::toHex(s.finish());
    CHECK(std::string_view(h.data(), h.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
