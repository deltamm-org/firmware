// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/input.h"

#include <cstdint>

namespace dmm::services {

/// A receive mode that takes the radio's one receiver over; Off: none, the
/// receiver follows the channel.
enum class DedicatedMode : std::uint8_t
{
    Off,
    Aprs,
    Count,
};

/// The mode's name for the screen and the log: "APRS".
const char *modeName(DedicatedMode mode);

/// Where a dedicated mode holds the receiver: analog FM at @c hz.
struct ReceiverLock
{
    std::uint32_t hz = 0;
    bool wide = true;

    bool operator==(const ReceiverLock &) const = default;
};

/// Which dedicated mode is on, where it holds the receiver, and what the
/// user's keys do meanwhile. The channel stays chosen underneath; leaving
/// the mode goes back to it.
class DedicatedModeState
{
public:
    enum class Change : std::uint8_t
    {
        TurnedOn,    // the receiver is to be locked
        Moved,       // another mode or frequency: the receiver is locked anew
        TurnedOff,   // the receiver goes back to the channel
        Unchanged,   // already so
        NoFrequency, // refused: the mode has no frequency
    };

    /// What a key or the knob does while a mode is on.
    enum class KeyUse : std::uint8_t
    {
        Usual,   // as on a channel
        List,    // opens or moves in the mode's own list
        Action,  // the mode's own action (PTT)
        Refused, // it would change the channel or the frequency
    };

    /// Turns @p mode on at @p lock; Off turns any mode off.
    Change turnOn(DedicatedMode mode, ReceiverLock lock);
    Change turnOff();

    bool on() const { return m_mode != DedicatedMode::Off; }
    DedicatedMode mode() const { return m_mode; }
    const ReceiverLock &lock() const { return m_lock; }

    /// What @p key does now; Usual for every key while no mode is on.
    KeyUse keyUse(Key key) const;
    /// What the knob does now.
    KeyUse knobUse() const;

private:
    DedicatedMode m_mode = DedicatedMode::Off;
    ReceiverLock m_lock;
};

} // namespace dmm::services
