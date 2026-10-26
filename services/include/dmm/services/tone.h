// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// A sub-audible tone: CTCSS in 0.1 Hz units, or a DCS code (the nine bits
/// of its three octal digits), normal or inverted.
struct Tone
{
    enum class Kind : std::uint8_t
    {
        None,
        Ctcss,
        Dcs,
    };
    Kind kind = Kind::None;
    std::uint16_t value = 0;
    bool inverted = false;

    bool operator==(const Tone &) const = default;
};

} // namespace dmm::services
