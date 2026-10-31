// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace board {

/// The serial link to the BT+APRS board, and the board's power.
class BoardAprsLink
{
public:
    /// Board off, link closed. Call once, before the scheduler starts.
    void configure();

    void start();
    void stop();
    std::size_t read(std::span<std::uint8_t> data);
    void write(std::span<const std::uint8_t> data);
    std::uint32_t lostBytes() const;

    /// Call from the link's interrupt.
    static void onInterrupt();
};

} // namespace board
