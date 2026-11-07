// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// What the radio task offers its roaming part: the few radio operations
// roaming needs, so that roaming decides and the radio does. Only the radio
// task's files include this.

#include "radio.h"

#include "dmm/lib/format.h"
#include "dmm/os/os.h"

#include <cstdint>

namespace dmm::app::radio::internal {

// Channel access through a repeater: how often a call or a search wakes the
// repeater in use before giving up.
inline constexpr int RepeaterWakeTries = 2;
// The colour code of a check that heard no burst.
inline constexpr std::uint8_t NoColourCode = 0xFF;

// The radio works through these frequencies and colour code from now on;
// the receiver follows at the next receive or listen.
void useRepeater(std::uint32_t receiveHz, std::uint32_t transmitHz, std::uint8_t colourCode);
// The receive frequency and colour code worked through now.
std::uint32_t tunedHz();
std::uint8_t tunedColourCode();
// The DMR ID calls and wake-ups go out with; 0 for none.
std::uint32_t ownDmrId();
bool transmitAllowed(std::uint32_t hz);

// Free for roaming: receiving, with no call, transmission or waiting
// message.
bool canRoam();
// Ends a call being heard and closes the speaker, before tuning away.
void leaveCall();
// Messages that come while roaming listens elsewhere wait for it; true when
// one of them should end it early.
bool searchInterrupted();

// What a check heard on the frequency it tuned to.
struct Heard
{
    bool cut = false;                       // a waiting message ended it first
    std::uint8_t colourCode = NoColourCode; // of a burst heard after slot timing
    int rssiDbm = 0;                        // the level read, also with nothing on the air
};
// Listens on the frequency set by useRepeater(), without transmitting: a
// carrier within @p carrierMs, then slot timing within @p timingMs, then a
// burst within @p burstMs.
Heard listenHere(os::Ticks carrierMs, os::Ticks timingMs, os::Ticks burstMs);
// Listens on it for a repeater already on the air: a carrier within
// @p carrierMs, then its timing as channel access allows. True if found.
bool onAirHere(os::Ticks carrierMs);
// Wakes the repeater on it as a call would, @p tries times at most.
enum class Wake : std::uint8_t
{
    Answered,
    NoAnswer,
    Refused, // the transmitter refused; @p problem says why
};
Wake wakeRepeater(int tries, const char *&problem);
// The level read now, averaged over a few readings.
int readLevel();
// Receives on the frequency set by useRepeater() again, after listening
// elsewhere.
void backHere();

lib::StaticString<12> mhz(std::uint32_t hz);

} // namespace dmm::app::radio::internal
