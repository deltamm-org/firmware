// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/dmr.h"

#include <doctest/doctest.h>

#include <array>

using dmm::services::dmr::CallType;
using dmm::services::dmr::FullLc;
using dmm::services::dmr::Target;

TEST_CASE("a group voice call's link control word")
{
    const FullLc lc = FullLc::voiceCall(Target{CallType::Group, 2441}, 7654321);
    std::array<std::uint8_t, FullLc::Bytes> b{};
    lc.encode(b);
    CHECK(b == std::array<std::uint8_t, 9>{0x00, 0x00, 0x00, 0x00, 0x09, 0x89, 0x74, 0xCB, 0xB1});
    const auto back = FullLc::decode(b);
    REQUIRE(back.has_value());
    CHECK(back->groupCall());
    CHECK(back->destination == 2441);
    CHECK(back->source == 7654321);
    CHECK_FALSE(back->emergency());
}

TEST_CASE("private and all calls, and short input")
{
    std::array<std::uint8_t, FullLc::Bytes> b{};
    FullLc::voiceCall(Target{CallType::Private, 1000001}, 7654321).encode(b);
    CHECK(b[0] == 0x03);
    CHECK(FullLc::decode(b)->privateCall());
    FullLc::voiceCall(Target{CallType::All, 0}, 1).encode(b);
    CHECK(FullLc::decode(b)->destination == 0xFFFFFF);
    b[0] = 0x80; // protect flag, group
    b[2] = 0x80; // emergency
    const auto lc = FullLc::decode(b);
    CHECK(lc->protect);
    CHECK(lc->groupCall());
    CHECK(lc->emergency());
    CHECK_FALSE(FullLc::decode(std::span(b.data(), 8)).has_value());
}
