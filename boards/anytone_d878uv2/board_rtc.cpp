// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/board/board_rtc.h"

#include "dmm/hal/gd32f30x/rtc.h"

namespace board {

using dmm::hal::gd32f30x::Rtc;

bool BoardRtc::start()
{
    return Rtc::start() != Rtc::Start::NoCrystal;
}

std::uint32_t BoardRtc::seconds()
{
    return Rtc::seconds();
}

dmm::services::ClockReading BoardRtc::read()
{
    const Rtc::Reading r = Rtc::read();
    return {r.seconds, static_cast<std::uint16_t>(r.cycles * 1000 / 32'768)};
}

void BoardRtc::setSeconds(std::uint32_t utc)
{
    Rtc::setSeconds(utc);
}

// One calibration step is one crystal cycle in 2^20: 10^9 / 2^20 ppb.
void BoardRtc::setRateCorrection(std::int32_t ppb)
{
    const std::int64_t scaled = static_cast<std::int64_t>(ppb) * (1 << 20);
    const std::int64_t half = scaled < 0 ? -500'000'000 : 500'000'000;
    Rtc::setCalibration(static_cast<int>((scaled + half) / 1'000'000'000));
}

std::int32_t BoardRtc::rateCorrection()
{
    return static_cast<std::int32_t>(static_cast<std::int64_t>(Rtc::calibration()) * 1'000'000'000 / (1 << 20));
}

} // namespace board
