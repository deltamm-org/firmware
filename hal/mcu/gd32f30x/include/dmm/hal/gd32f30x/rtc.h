// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstdint>

namespace dmm::hal::gd32f30x {

/// The real-time clock in the backup domain: a 32-bit seconds counter on
/// the 32.768 kHz crystal (LXTAL), kept running by the backup supply while
/// the MCU is off. Backup data register 0 holds a marker once the clock has
/// been set up, so later starts only resynchronise.
class Rtc
{
public:
    enum class Start : std::uint8_t
    {
        Running,   // set up before; the counter kept running
        Started,   // set up now; the counter starts from 0
        NoCrystal, // the crystal did not start; no clock
    };

    /// Call once, before the scheduler starts or from one task.
    static Start start();
    static std::uint32_t seconds();
    /// The seconds and the crystal cycles into the next one (0..32767).
    struct Reading
    {
        std::uint32_t seconds;
        std::uint16_t cycles;
    };
    static Reading read();
    static void setSeconds(std::uint32_t seconds);

    /// The rate calibration: @p steps crystal cycles added (positive) or
    /// removed (negative) in every 2^20, about 0.954 ppm each, up to
    /// MaxCalibrationSteps either way. Kept in the backup domain.
    static void setCalibration(int steps);
    static int calibration();
    static constexpr int MaxCalibrationSteps = 127;

    /// The marker in backup data register 0 of a clock that has been set up.
    static constexpr std::uint16_t Marker = 0x5050;
    /// A second marker value other firmware may leave; also means set up.
    static constexpr std::uint16_t OtherMarker = 0x6060;
};

} // namespace dmm::hal::gd32f30x
