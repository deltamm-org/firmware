// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "pins.h"

#include "dmm/hal/gd32f30x/parallel_host_port.h"
#include "dmm/services/baseband.h"
#include "dmm/services/sct3258_baseband.h"

namespace board {

/// Power, reset and host port of the DMR baseband chip.
class BoardBaseband final : public dmm::services::BasebandBoard
{
public:
    /// At start: the chip unpowered and held in reset, all its lines low.
    void configurePins();

    void powerUpAndReset() override;

    dmm::hal::HostPortBus &bus() { return m_port; }

    /// A busy wait for the chip driver's polling that sleeps 1 ms after
    /// every 1 ms of waiting, so lower-priority tasks (and the watchdog
    /// they feed) still run while it waits for an answer.
    static void pollDelayUs(unsigned us);

private:
    dmm::hal::gd32f30x::ParallelHostPort m_port{BasebandPort};
};

/// The TLV320AIC3204 codec behind the baseband chip: speaker on the
/// headphone-left driver, microphone on the left input.
const dmm::services::Sct3258Baseband::Audio &basebandAudio();

} // namespace board
