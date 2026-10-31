// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/clock.h"

#include <chrono>
#include <cstdint>

/// The PC's clock, plus whatever the app sets it ahead or back by.
class SimRtc
{
public:
    bool start() { return true; }
    std::uint32_t seconds() const { return static_cast<std::uint32_t>(pcMs() / 1000 + m_offset); }
    dmm::services::ClockReading read() const
    {
        const std::int64_t ms = pcMs();
        return {static_cast<std::uint32_t>(ms / 1000 + m_offset), static_cast<std::uint16_t>(ms % 1000)};
    }
    void setSeconds(std::uint32_t utc) { m_offset = static_cast<std::int64_t>(utc) - pcMs() / 1000; }
    /// Kept only: the PC's clock needs none.
    void setRateCorrection(std::int32_t ppb) { m_ppb = ppb; }
    std::int32_t rateCorrection() const { return m_ppb; }

private:
    static std::int64_t pcMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::int64_t m_offset = 0;
    std::int32_t m_ppb = 0;
};
