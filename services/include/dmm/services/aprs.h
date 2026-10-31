// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/format.h"
#include "dmm/services/ax25.h"

#include <cstdint>
#include <optional>
#include <string_view>

/// APRS position reports (APRS Protocol Reference 1.0.1, chapters 8-10):
/// building one from our position, and reading others' in the plain,
/// compressed and Mic-E forms.
namespace dmm::services::aprs {

/// A station's reported position. Angles are degrees times 10^7, north and
/// east positive.
struct Position
{
    std::int32_t latitudeE7 = 0;
    std::int32_t longitudeE7 = 0;
    /// The symbol: table '/' (primary) or '\\' (alternate, or an overlay
    /// character) and the symbol code, such as '>' for a car.
    char symbolTable = '/';
    char symbol = '>';
    bool hasCourse = false;
    /// Course in degrees (1-360, 0 = unknown) and speed in knots.
    std::uint16_t courseDeg = 0;
    std::uint16_t speedKnots = 0;
    bool hasAltitude = false;
    /// Altitude in metres above mean sea level.
    std::int32_t altitudeM = 0;
    /// The free text after the position data; longer text is cut. Position
    /// reports allow 43 characters.
    static constexpr std::size_t MaxComment = 64;
    lib::StaticString<MaxComment> comment;
};

/// The info field of a plain position report without a timestamp:
/// "!DDMM.mmN/DDDMM.mmE>CCC/SSS/A=FFFFFFcomment", course and speed only
/// when @p p has them, the altitude in feet only when it has one.
/// @p messaging uses '=' instead of '!', saying the station can receive
/// messages. Written into @p out, which it replaces.
void positionReport(const Position &p, bool messaging, lib::StaticString<ax25::MaxInfo> &out);

/// The position in @p frame, if it is a position report in any of the
/// three forms (with or without a timestamp).
std::optional<Position> readPosition(const ax25::Frame &frame);

} // namespace dmm::services::aprs
