// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/aprs.h"
#include "dmm/services/ax25.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <string>

using namespace dmm::services;

namespace {

ax25::Frame frame(std::string_view source, std::string_view destination, std::string_view info)
{
    ax25::Frame f;
    f.source = *ax25::parseAddress(source);
    f.destination = *ax25::parseAddress(destination);
    f.info << info;
    return f;
}

std::string text(const ax25::Frame &f)
{
    dmm::lib::StaticString<400> s;
    ax25::writeText(s, f);
    return std::string(s.view());
}

} // namespace

TEST_CASE("ax25 frame check is CRC-16/X.25")
{
    const std::string check = "123456789";
    CHECK(ax25::frameCheck({reinterpret_cast<const std::uint8_t *>(check.data()), check.size()}) == 0x906E);
}

TEST_CASE("ax25 addresses parse and print")
{
    const auto a = ax25::parseAddress("N0CALL-7");
    REQUIRE(a);
    CHECK(a->call.view() == "N0CALL");
    CHECK(a->ssid == 7);
    CHECK(ax25::addressText(*a).view() == "N0CALL-7");
    const auto w = ax25::parseAddress("WIDE2-1*");
    REQUIRE(w);
    CHECK(w->repeated);
    CHECK(ax25::addressText(*w).view() == "WIDE2-1*");
    CHECK(ax25::addressText(*ax25::parseAddress("APZDMM")).view() == "APZDMM");
    CHECK_FALSE(ax25::parseAddress(""));
    CHECK_FALSE(ax25::parseAddress("TOOLONG1"));
    CHECK_FALSE(ax25::parseAddress("n0call"));
    CHECK_FALSE(ax25::parseAddress("N0CALL-16"));
    CHECK_FALSE(ax25::parseAddress("N0CALL-07"));
    CHECK_FALSE(ax25::parseAddress("N0CALL-"));
}

TEST_CASE("ax25 frames encode with shifted addresses and decode back")
{
    ax25::Frame f = frame("N0CALL-7", "APZDMM", "!6010.25N/02456.30E>Test");
    f.path.pushBack(*ax25::parseAddress("WIDE1-1"));
    f.path.pushBack(*ax25::parseAddress("WIDE2-1"));
    std::array<std::uint8_t, ax25::MaxFrameBytes> bytes{};
    const std::size_t n = ax25::encode(f, bytes);
    REQUIRE(n == 7 * 4 + 2 + 24 + 2);
    CHECK(bytes[0] == 'A' << 1);
    CHECK(bytes[6] == 0xE0);         // destination: C bit, reserved bits, SSID 0
    CHECK(bytes[13] == (0x60 | 14)); // source: SSID 7, not the last address
    CHECK(bytes[27] == (0x60 | 2 | 1));
    CHECK(bytes[28] == 0x03);
    CHECK(bytes[29] == 0xF0);
    const std::uint16_t check = ax25::frameCheck({bytes.data(), n - 2});
    CHECK(bytes[n - 2] == (check & 0xFF));
    CHECK(bytes[n - 1] == check >> 8);

    ax25::Frame back;
    REQUIRE(ax25::decode({bytes.data(), n}, back));
    CHECK(text(back) == "N0CALL-7>APZDMM,WIDE1-1,WIDE2-1:!6010.25N/02456.30E>Test");

    bytes[30] ^= 1;
    CHECK_FALSE(ax25::decode({bytes.data(), n}, back));
}

TEST_CASE("ax25 decode reads repeated digipeaters and rejects other frames")
{
    ax25::Frame f = frame("N0CALL", "APZDMM", ">status");
    f.path.pushBack(*ax25::parseAddress("WIDE1*"));
    std::array<std::uint8_t, ax25::MaxFrameBytes> bytes{};
    const std::size_t n = ax25::encode(f, bytes);
    ax25::Frame back;
    REQUIRE(ax25::decode({bytes.data(), n}, back));
    CHECK(text(back) == "N0CALL>APZDMM,WIDE1*:>status");

    // Not a UI frame: control 0x13, check recomputed.
    bytes[21] = 0x13;
    const std::uint16_t check = ax25::frameCheck({bytes.data(), n - 2});
    bytes[n - 2] = static_cast<std::uint8_t>(check);
    bytes[n - 1] = static_cast<std::uint8_t>(check >> 8);
    CHECK_FALSE(ax25::decode({bytes.data(), n}, back));
    CHECK_FALSE(ax25::decode({bytes.data(), 10}, back));
}

TEST_CASE("aprs position report from our position")
{
    aprs::Position p;
    p.latitudeE7 = 601708333;  // 60 10.25 N
    p.longitudeE7 = 249383333; // 24 56.30 E
    dmm::lib::StaticString<ax25::MaxInfo> info;
    aprs::positionReport(p, false, info);
    CHECK(info.view() == "!6010.25N/02456.30E>");
    p.hasCourse = true;
    p.courseDeg = 90;
    p.speedKnots = 20;
    p.hasAltitude = true;
    p.altitudeM = 50;
    p.comment << "Test";
    aprs::positionReport(p, true, info);
    CHECK(info.view() == "=6010.25N/02456.30E>090/020/A=000164Test");
    p.latitudeE7 = -335000000;
    p.longitudeE7 = -1120000000;
    p.hasCourse = false;
    p.altitudeM = -10;
    p.comment.clear();
    p.symbolTable = '\\';
    p.symbol = 'k';
    aprs::positionReport(p, false, info);
    CHECK(info.view() == "!3330.00S\\11200.00Wk/A=-00033");
}

TEST_CASE("aprs plain position reports read back, with or without a timestamp")
{
    const auto p = aprs::readPosition(frame("N0CALL", "APZDMM", "=6010.25N/02456.30E>090/020/A=000164Test\r"));
    REQUIRE(p);
    CHECK(p->latitudeE7 == 601708333);
    CHECK(p->longitudeE7 == 249383333);
    CHECK(p->symbolTable == '/');
    CHECK(p->symbol == '>');
    CHECK(p->hasCourse);
    CHECK(p->courseDeg == 90);
    CHECK(p->speedKnots == 20);
    CHECK(p->hasAltitude);
    CHECK(p->altitudeM == 50);
    CHECK(p->comment.view() == "Test");

    const auto t = aprs::readPosition(frame("N0CALL", "APZDMM", "@092345z4903.50N/07201.75W-Home /A=001000 base"));
    REQUIRE(t);
    CHECK(t->latitudeE7 == 490583333);
    CHECK(t->longitudeE7 == -720291666);
    CHECK(t->symbol == '-');
    CHECK_FALSE(t->hasCourse);
    CHECK(t->altitudeM == 305);
    CHECK(t->comment.view() == "Home  base");

    // Ambiguity: the last digits left as spaces.
    const auto a = aprs::readPosition(frame("N0CALL", "APZDMM", "!4903.  N/07201.  W-"));
    REQUIRE(a);
    CHECK(a->latitudeE7 == 490500000);

    CHECK_FALSE(aprs::readPosition(frame("N0CALL", "APZDMM", "!4903.50X/07201.75W-")));
    CHECK_FALSE(aprs::readPosition(frame("N0CALL", "APZDMM", ">status text")));
    CHECK_FALSE(aprs::readPosition(frame("N0CALL", "APZDMM", "")));
}

TEST_CASE("aprs compressed position reports read back")
{
    const auto p = aprs::readPosition(frame("N0CALL", "APZDMM", "=/5L!!<*e7>7P[comment"));
    REQUIRE(p);
    // Base 91 resolves to about half a metre.
    CHECK(std::abs(p->latitudeE7 - 495000000) < 100);
    CHECK(std::abs(p->longitudeE7 - -727500000) < 100);
    CHECK(p->symbol == '>');
    CHECK(p->hasCourse);
    CHECK(p->courseDeg == 88);
    CHECK(p->speedKnots == 36);
    CHECK(p->comment.view() == "comment");
}

TEST_CASE("aprs Mic-E reports read back")
{
    // 60 10.25 N, 24 56.30 E; 20 knots at 90 degrees; 50 m; a Yaesu-style
    // radio type before the altitude and after the comment.
    const auto p = aprs::readPosition(frame("N0CALL-7", "VP1P25", "`4T:n v>/`\"4I}Test_0\r"));
    REQUIRE(p);
    CHECK(p->latitudeE7 == 601708333);
    CHECK(p->longitudeE7 == 249383333);
    CHECK(p->hasCourse);
    CHECK(p->speedKnots == 20);
    CHECK(p->courseDeg == 90);
    CHECK(p->symbol == '>');
    CHECK(p->symbolTable == '/');
    CHECK(p->hasAltitude);
    CHECK(p->altitudeM == 50);
    CHECK(p->comment.view() == "Test");

    // South and west, longitude offset: 33 25.64 S, 112 07.74 W.
    const auto s = aprs::readPosition(frame("N0CALL", "3325VT", "'(#fl \x1c"
                                                                "k/"));
    REQUIRE(s);
    CHECK(s->latitudeE7 == -334273333);
    CHECK(s->longitudeE7 == -1121290000);
    CHECK_FALSE(s->hasCourse);
    CHECK(s->symbol == 'k');

    CHECK_FALSE(aprs::readPosition(frame("N0CALL", "APZDMM", "`4T:n v>/")));
    CHECK_FALSE(aprs::readPosition(frame("N0CALL", "VP1P25", "`4T")));
}
