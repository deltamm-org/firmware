// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/clock.h"

#include <cstdint>

namespace board {

/// The MCU's real-time clock on the 32.768 kHz crystal, kept running from
/// the battery while the radio is off.
class BoardRtc
{
public:
    bool start();
    std::uint32_t seconds();
    dmm::services::ClockReading read();
    void setSeconds(std::uint32_t utc);
    void setRateCorrection(std::int32_t ppb);
    std::int32_t rateCorrection();
};

} // namespace board
