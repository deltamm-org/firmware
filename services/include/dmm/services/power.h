// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/os/os.h"

#include <cstdint>
#include <string_view>

namespace dmm::services::power {

/// Why the radio must stay fully awake. Battery save may start only when
/// none is held. Each user of the receiver, the CPU or the display holds
/// its own reason while it needs it.
enum class AwakeReason : std::uint8_t
{
    Transmit,
    Carrier,
    Call,
    Scan,
    Tone,
    UsbLink,
    Display,
    Count,
};

std::string_view name(AwakeReason reason);

void hold(AwakeReason reason);
void release(AwakeReason reason);
bool isHeld(AwakeReason reason);

/// Bit n set = reason n held.
std::uint32_t heldMask();

/// A moment of use, such as a key press: the idle time starts again.
void activity();

/// Milliseconds since the last release or activity; 0 while any reason is
/// held.
os::Ticks idleForMs();

} // namespace dmm::services::power
