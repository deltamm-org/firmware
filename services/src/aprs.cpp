// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/aprs.h"

#include <cstdlib>

namespace dmm::services::aprs {

namespace {

constexpr std::int64_t E7 = 10'000'000;

// Hundredths of a minute in an angle, rounded.
std::uint32_t hundredthsOfMinute(std::int32_t e7)
{
    const std::int64_t a = std::llabs(static_cast<std::int64_t>(e7));
    return static_cast<std::uint32_t>((a * 6000 + E7 / 2) / E7);
}

// "DDMM.mm" or "DDDMM.mm" and the hemisphere letter.
void writeAngle(lib::StaticString<ax25::MaxInfo> &out, std::int32_t e7, std::uint8_t degreeDigits, char positive, char negative)
{
    const std::uint32_t h = hundredthsOfMinute(e7);
    out << lib::zeroPadded(h / 6000, degreeDigits) << lib::zeroPadded(h % 6000 / 100, 2) << '.'
        << lib::zeroPadded(h % 100, 2) << (e7 < 0 ? negative : positive);
}

// A run of decimal digits; a space counts as 0 (position ambiguity).
std::optional<std::int32_t> digits(std::string_view text)
{
    std::int32_t value = 0;
    for (char c : text) {
        if (c == ' ') {
            c = '0';
        }
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

std::string_view slice(std::string_view text, std::size_t from, std::size_t count)
{
    return {text.data() + from, count};
}

std::int32_t angleE7(std::int32_t degrees, std::int32_t hundredths, bool negative)
{
    const std::int64_t value = degrees * E7 + hundredths * E7 / 6000;
    return static_cast<std::int32_t>(negative ? -value : value);
}

std::int32_t base91(std::string_view text)
{
    std::int32_t value = 0;
    for (char c : text) {
        value = value * 91 + (c - 33);
    }
    return value;
}

bool isBase91(std::string_view text)
{
    for (char c : text) {
        if (c < 33 || c > 123) {
            return false;
        }
    }
    return true;
}

std::string_view trimEnd(std::string_view text)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    return text;
}

// Moves a "/A=FFFFFF" altitude (feet) out of the comment.
void takeAltitude(Position &p, std::string_view comment)
{
    const std::size_t at = comment.find("/A=");
    if (at != std::string_view::npos && at + 9 <= comment.size()) {
        const std::string_view field = slice(comment, at + 3, 6);
        const bool negative = field[0] == '-';
        if (const auto feet = digits(negative ? slice(field, 1, 5) : field)) {
            p.hasAltitude = true;
            const std::int32_t metres = (*feet * 3048 + 5000) / 10000;
            p.altitudeM = negative ? -metres : metres;
            p.comment << slice(comment, 0, at) << slice(comment, at + 9, comment.size() - at - 9);
            return;
        }
    }
    p.comment << comment;
}

std::optional<Position> readPlain(std::string_view body)
{
    if (body.size() < 19 || (body[7] != 'N' && body[7] != 'S') || (body[17] != 'E' && body[17] != 'W') || body[4] != '.' ||
        body[14] != '.') {
        return std::nullopt;
    }
    const auto latDeg = digits(slice(body, 0, 2));
    const auto latMin = digits(slice(body, 2, 2));
    const auto latHund = digits(slice(body, 5, 2));
    const auto lonDeg = digits(slice(body, 9, 3));
    const auto lonMin = digits(slice(body, 12, 2));
    const auto lonHund = digits(slice(body, 15, 2));
    if (!latDeg || !latMin || !latHund || !lonDeg || !lonMin || !lonHund || *latDeg > 90 || *lonDeg > 180) {
        return std::nullopt;
    }
    Position p;
    p.latitudeE7 = angleE7(*latDeg, *latMin * 100 + *latHund, body[7] == 'S');
    p.longitudeE7 = angleE7(*lonDeg, *lonMin * 100 + *lonHund, body[17] == 'W');
    p.symbolTable = body[8];
    p.symbol = body[18];
    std::string_view rest(body.data() + 19, body.size() - 19);
    if (rest.size() >= 7 && rest[3] == '/') {
        const auto course = digits(slice(rest, 0, 3));
        const auto speed = digits(slice(rest, 4, 3));
        if (course && speed && *course <= 360) {
            p.hasCourse = true;
            p.courseDeg = static_cast<std::uint16_t>(*course);
            p.speedKnots = static_cast<std::uint16_t>(*speed);
            rest.remove_prefix(7);
        }
    }
    takeAltitude(p, trimEnd(rest));
    return p;
}

std::optional<Position> readCompressed(std::string_view body)
{
    if (body.size() < 13 || !isBase91(slice(body, 1, 8))) {
        return std::nullopt;
    }
    Position p;
    p.symbolTable = body[0];
    p.latitudeE7 = static_cast<std::int32_t>(90 * E7 - static_cast<std::int64_t>(base91(slice(body, 1, 4))) * E7 / 380926);
    p.longitudeE7 = static_cast<std::int32_t>(-180 * E7 + static_cast<std::int64_t>(base91(slice(body, 5, 4))) * E7 / 190463);
    p.symbol = body[9];
    const char c = body[10];
    const char s = body[11];
    const auto t = static_cast<std::uint8_t>(body[12] - 33);
    // Course and speed unless the field is empty, holds an altitude or a
    // radio range.
    if (c != ' ' && (t & 0x18) != 0x10 && c >= '!' && c <= 'z') {
        p.hasCourse = true;
        p.courseDeg = static_cast<std::uint16_t>((c - 33) * 4);
        double speed = 1.0;
        for (int i = 0; i < s - 33; ++i) {
            speed *= 1.08;
        }
        p.speedKnots = static_cast<std::uint16_t>(speed - 1.0 + 0.5);
    }
    takeAltitude(p, trimEnd(std::string_view(body.data() + 13, body.size() - 13)));
    return p;
}

// One destination character of a Mic-E report: its latitude digit (a space
// for ambiguity counts as 0), or nothing if it cannot be one.
std::optional<std::int32_t> micEDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'J') {
        return c - 'A';
    }
    if (c >= 'P' && c <= 'Y') {
        return c - 'P';
    }
    if (c == 'K' || c == 'L' || c == 'Z') {
        return 0;
    }
    return std::nullopt;
}

std::optional<Position> readMicE(const ax25::Frame &frame, std::string_view info)
{
    const std::string_view dest = frame.destination.call.view();
    if (dest.size() != 6 || info.size() < 9) {
        return std::nullopt;
    }
    std::int32_t lat[6];
    for (std::size_t i = 0; i < 6; ++i) {
        const auto d = micEDigit(dest[i]);
        if (!d) {
            return std::nullopt;
        }
        lat[i] = *d;
    }
    const bool north = dest[3] >= 'P';
    const bool offset = dest[4] >= 'P';
    const bool west = dest[5] >= 'P';

    std::int32_t lonDeg = info[1] - 28 + (offset ? 100 : 0);
    if (lonDeg >= 180 && lonDeg <= 189) {
        lonDeg -= 80;
    } else if (lonDeg >= 190 && lonDeg <= 199) {
        lonDeg -= 190;
    }
    std::int32_t lonMin = info[2] - 28;
    if (lonMin >= 60) {
        lonMin -= 60;
    }
    const std::int32_t lonHund = info[3] - 28;
    if (lonDeg < 0 || lonDeg > 179 || lonMin < 0 || lonMin > 59 || lonHund < 0 || lonHund > 99) {
        return std::nullopt;
    }

    Position p;
    p.latitudeE7 = angleE7(lat[0] * 10 + lat[1], (lat[2] * 10 + lat[3]) * 100 + lat[4] * 10 + lat[5], !north);
    p.longitudeE7 = angleE7(lonDeg, lonMin * 100 + lonHund, west);

    std::int32_t speed = (info[4] - 28) * 10 + (info[5] - 28) / 10;
    std::int32_t course = (info[5] - 28) % 10 * 100 + (info[6] - 28);
    if (speed >= 800) {
        speed -= 800;
    }
    if (course >= 400) {
        course -= 400;
    }
    if (speed >= 0 && course >= 0 && course <= 360 && (speed != 0 || course != 0)) {
        p.hasCourse = true;
        p.courseDeg = static_cast<std::uint16_t>(course);
        p.speedKnots = static_cast<std::uint16_t>(speed);
    }
    p.symbol = info[7];
    p.symbolTable = info[8];

    std::string_view rest = trimEnd(std::string_view(info.data() + 9, info.size() - 9));
    // A leading character names the kind of radio; some add a suffix.
    char kind = 0;
    if (!rest.empty() && (rest[0] == '>' || rest[0] == ']' || rest[0] == '`' || rest[0] == '\'')) {
        kind = rest[0];
        rest.remove_prefix(1);
    }
    if (rest.size() >= 4 && rest[3] == '}' && isBase91(slice(rest, 0, 3))) {
        p.hasAltitude = true;
        p.altitudeM = base91(slice(rest, 0, 3)) - 10000;
        rest.remove_prefix(4);
    }
    if (kind == '`' && rest.size() >= 2 && rest[rest.size() - 2] == '_') {
        rest.remove_suffix(2);
    } else if ((kind == ']' || kind == '>') && !rest.empty() && (rest.back() == '=' || rest.back() == '^')) {
        rest.remove_suffix(1);
    }
    p.comment << rest;
    return p;
}

} // namespace

void positionReport(const Position &p, bool messaging, lib::StaticString<ax25::MaxInfo> &out)
{
    out.clear();
    out << (messaging ? '=' : '!');
    writeAngle(out, p.latitudeE7, 2, 'N', 'S');
    out << p.symbolTable;
    writeAngle(out, p.longitudeE7, 3, 'E', 'W');
    out << p.symbol;
    if (p.hasCourse) {
        out << lib::zeroPadded(p.courseDeg % 361, 3) << '/' << lib::zeroPadded(p.speedKnots > 999 ? 999 : p.speedKnots, 3);
    }
    if (p.hasAltitude) {
        const std::int32_t feet = (p.altitudeM * 10000 + (p.altitudeM < 0 ? -1524 : 1524)) / 3048;
        out << "/A=";
        if (feet < 0) {
            out << '-' << lib::zeroPadded(static_cast<std::uint32_t>(-feet > 99999 ? 99999 : -feet), 5);
        } else {
            out << lib::zeroPadded(static_cast<std::uint32_t>(feet > 999999 ? 999999 : feet), 6);
        }
    }
    out << p.comment.view();
}

std::optional<Position> readPosition(const ax25::Frame &frame)
{
    const std::string_view info = frame.info.view();
    if (info.empty()) {
        return std::nullopt;
    }
    std::string_view body;
    switch (info[0]) {
    case '!':
    case '=':
        body = slice(info, 1, info.size() - 1);
        break;
    case '/':
    case '@':
        // After a seven-character timestamp.
        if (info.size() < 8) {
            return std::nullopt;
        }
        body = slice(info, 8, info.size() - 8);
        break;
    case '`':
    case '\'':
        return readMicE(frame, info);
    default:
        return std::nullopt;
    }
    if (body.empty()) {
        return std::nullopt;
    }
    return (body[0] >= '0' && body[0] <= '9') || body[0] == ' ' ? readPlain(body) : readCompressed(body);
}

} // namespace dmm::services::aprs
