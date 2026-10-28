// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::hal::gd32f30x {

/// A periodic interrupt from TIMER6, a basic timer: one update interrupt
/// every period while running. The board calls onInterrupt() from the
/// TIMER6 handler.
class StepTimer
{
public:
    /// Sets the timer up, stopped, with its interrupt at @p irqPriority.
    void configure(unsigned irqPriority);
    /// Starts it with an interrupt every @p periodUs (1..65535), the first
    /// one a period from now.
    void start(std::uint32_t periodUs);
    void stop();
    /// Clears the interrupt; true if it was this timer's update.
    bool takeInterrupt();
};

} // namespace dmm::hal::gd32f30x
