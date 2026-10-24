// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/lib/strings.h"

#include <doctest/doctest.h>

using dmm::lib::endsWith;
using dmm::lib::parseNumber;
using dmm::lib::startsWith;
using dmm::lib::takeWord;

TEST_CASE("startsWith and endsWith match whole prefixes and suffixes")
{
    CHECK(startsWith("autooff 5", "autooff "));
    CHECK(startsWith("abc", ""));
    CHECK(startsWith("", ""));
    CHECK_FALSE(startsWith("auto", "autooff "));
    CHECK_FALSE(startsWith("xautooff", "autooff"));

    CHECK(endsWith("pin pa7 1", " 1"));
    CHECK(endsWith("abc", ""));
    CHECK_FALSE(endsWith("1", " 1"));
    CHECK_FALSE(endsWith("pin pa7 0", " 1"));
}

TEST_CASE("takeWord splits at single spaces")
{
    std::string_view text = "fm 1f 0a";
    CHECK(takeWord(text) == "fm");
    CHECK(takeWord(text) == "1f");
    CHECK(text == "0a");
    CHECK(takeWord(text) == "0a");
    CHECK(text.empty());
    CHECK(takeWord(text).empty());
}

TEST_CASE("parseNumber reads decimal and lower-case hex")
{
    CHECK(parseNumber("4095", 10) == 4095);
    CHECK(parseNumber("1f", 16) == 0x1F);
    CHECK_FALSE(parseNumber("1f", 10).has_value());
    CHECK_FALSE(parseNumber("1F", 16).has_value());
    CHECK_FALSE(parseNumber("", 10).has_value());
    CHECK_FALSE(parseNumber("123456789", 10).has_value());
    CHECK_FALSE(parseNumber("ffffffff", 16).has_value());
}
