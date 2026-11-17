// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/dedicated_mode.h"

namespace dmm::services {

const char *modeName(DedicatedMode mode)
{
    switch (mode) {
    case DedicatedMode::Aprs:
        return "APRS";
    case DedicatedMode::Off:
    case DedicatedMode::Count:
        break;
    }
    return "";
}

DedicatedModeState::Change DedicatedModeState::turnOn(DedicatedMode mode, ReceiverLock lock)
{
    if (mode == DedicatedMode::Off || mode >= DedicatedMode::Count) {
        return turnOff();
    }
    if (lock.hz == 0) {
        return Change::NoFrequency;
    }
    if (mode == m_mode && lock == m_lock) {
        return Change::Unchanged;
    }
    const bool wasOn = on();
    m_mode = mode;
    m_lock = lock;
    return wasOn ? Change::Moved : Change::TurnedOn;
}

DedicatedModeState::Change DedicatedModeState::turnOff()
{
    if (!on()) {
        return Change::Unchanged;
    }
    m_mode = DedicatedMode::Off;
    m_lock = {};
    return Change::TurnedOff;
}

DedicatedModeState::KeyUse DedicatedModeState::keyUse(Key key) const
{
    if (!on()) {
        return KeyUse::Usual;
    }
    switch (key) {
    case Key::Up:
    case Key::Down:
    case Key::Front2:
        return KeyUse::List;
    case Key::Ptt:
        // APRS sends a beacon; a mode that only receives would refuse.
        return m_mode == DedicatedMode::Aprs ? KeyUse::Action : KeyUse::Refused;
    case Key::Front1: // zones
    case Key::Hash:   // VFO
        return KeyUse::Refused;
    default:
        break;
    }
    if (key >= Key::Digit0 && key <= Key::Digit9) {
        return KeyUse::Refused;
    }
    return KeyUse::Usual;
}

DedicatedModeState::KeyUse DedicatedModeState::knobUse() const
{
    return on() ? KeyUse::List : KeyUse::Usual;
}

} // namespace dmm::services
