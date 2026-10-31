// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstdint>
#include <span>

namespace board {

/// Packet radio AFSK on the beep pin, which also reaches the transmitter's
/// modulation input when the microphone is muted.
class BoardModem
{
public:
    bool send(std::span<const std::uint8_t> frame, std::uint16_t leadMs, std::uint8_t levelPercent);
    bool sending();
};

} // namespace board
