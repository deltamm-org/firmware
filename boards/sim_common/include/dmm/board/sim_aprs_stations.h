// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/aprs.h"
#include "dmm/services/ax25.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

/// Made-up stations near the simulated GPS position, heard in turns: one
/// in each 10 s slot of the clock.
namespace sim_aprs {

inline constexpr std::chrono::seconds Slot{10};

/// The slot @p t falls in.
inline std::int64_t slotAt(std::chrono::steady_clock::time_point t)
{
    return t.time_since_epoch() / Slot;
}

/// The frame sent in @p slot, from the first address to the check, into
/// @p out; returns its length.
inline std::size_t frame(std::int64_t slot, std::span<std::uint8_t, dmm::services::ax25::MaxFrameBytes> out)
{
    namespace ax25 = dmm::services::ax25;
    const auto which = static_cast<unsigned>(slot % 4);
    ax25::Frame f;
    constexpr const char *Calls[] = {"N0CALL-7", "N0CALL-9", "N0CALL-1", "N0CALL-3"};
    f.source = *ax25::parseAddress(Calls[which]);
    f.destination = *ax25::parseAddress("APZSIM");
    f.path.pushBack(*ax25::parseAddress("WIDE1-1*"));
    if (which == 2) {
        f.info << ">Made-up status text";
    } else {
        dmm::services::aprs::Position p;
        p.latitudeE7 = 514780000 + static_cast<std::int32_t>(which) * 90000;
        p.longitudeE7 = -15000 + static_cast<std::int32_t>(which) * 160000;
        p.symbol = which == 0 ? '>' : '-';
        p.hasCourse = which == 0;
        p.courseDeg = 90;
        p.speedKnots = 27;
        p.hasAltitude = true;
        p.altitudeM = 40;
        p.comment << (which == 0 ? "Made-up car" : which == 3 ? "Made-up weak station"
                                                              : "Made-up home");
        dmm::services::aprs::positionReport(p, false, f.info);
    }
    return ax25::encode(f, out);
}

} // namespace sim_aprs
