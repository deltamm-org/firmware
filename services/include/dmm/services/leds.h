// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// Status LEDs. A board without one of them ignores it.
class Leds
{
public:
    enum class Led : std::uint8_t
    {
        Red,
        Green,
        Blue,
    };

    virtual void set(Led led, bool on) = 0;

protected:
    ~Leds() = default;
};

} // namespace dmm::services
