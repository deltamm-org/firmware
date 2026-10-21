// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/power.h"

namespace dmm::services::power {

namespace {

os::Mutex stateLock;
std::uint32_t held = 0;
os::Ticks lastBusy = 0;

constexpr std::uint32_t bit(AwakeReason reason)
{
    return 1u << static_cast<unsigned>(reason);
}

} // namespace

std::string_view name(AwakeReason reason)
{
    switch (reason) {
    case AwakeReason::Transmit:
        return "transmit";
    case AwakeReason::Carrier:
        return "carrier";
    case AwakeReason::Call:
        return "call";
    case AwakeReason::Scan:
        return "scan";
    case AwakeReason::Tone:
        return "tone";
    case AwakeReason::UsbLink:
        return "usb";
    case AwakeReason::Display:
        return "display";
    case AwakeReason::Count:
        break;
    }
    return "?";
}

void hold(AwakeReason reason)
{
    stateLock.lock();
    held |= bit(reason);
    stateLock.unlock();
}

void release(AwakeReason reason)
{
    stateLock.lock();
    if ((held & bit(reason)) != 0) {
        held &= ~bit(reason);
        if (held == 0) {
            lastBusy = os::nowMs();
        }
    }
    stateLock.unlock();
}

bool isHeld(AwakeReason reason)
{
    return (heldMask() & bit(reason)) != 0;
}

std::uint32_t heldMask()
{
    stateLock.lock();
    const std::uint32_t mask = held;
    stateLock.unlock();
    return mask;
}

void activity()
{
    stateLock.lock();
    lastBusy = os::nowMs();
    stateLock.unlock();
}

os::Ticks idleForMs()
{
    stateLock.lock();
    const os::Ticks idle = held != 0 ? 0 : os::nowMs() - lastBusy;
    stateLock.unlock();
    return idle;
}

} // namespace dmm::services::power
