// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <concepts>
#include <cstdint>

namespace dmm::services {

/// A clock reading: UTC seconds since 1970-01-01 and the milliseconds into
/// that second.
struct ClockReading
{
    std::uint32_t seconds = 0;
    std::uint16_t milliseconds = 0; // 0..999
};

/// A clock that keeps UTC while the radio is off; the board names the type
/// (dmm/board/parts.h). Used by one task.
///
/// - start(): starts the clock if it is not running yet. True when it
///   runs; false without one.
/// - seconds(): UTC seconds since 1970-01-01.
/// - read(): the same to the millisecond.
/// - setSeconds(utc): sets it; the fraction of the second runs on.
/// - setRateCorrection(ppb): runs the clock faster by @p ppb parts per
///   billion (negative: slower), as near as the clock can; kept while the
///   radio is off.
/// - rateCorrection(): the correction in effect, in parts per billion.
template <class T>
concept RealTimeClock = requires(T &clock, std::uint32_t utc, std::int32_t ppb) {
    { clock.start() } -> std::same_as<bool>;
    { clock.seconds() } -> std::same_as<std::uint32_t>;
    { clock.read() } -> std::same_as<ClockReading>;
    clock.setSeconds(utc);
    clock.setRateCorrection(ppb);
    { clock.rateCorrection() } -> std::same_as<std::int32_t>;
};

/// A calendar date and time of day.
struct DateTime
{
    std::uint16_t year = 1970;
    std::uint8_t month = 1; // 1..12
    std::uint8_t day = 1;   // 1..31
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;

    bool operator==(const DateTime &) const = default;
};

/// Seconds since 1970-01-01 00:00 as a date and time.
DateTime dateTime(std::uint32_t seconds);
/// A date and time from 1970 to 2105 as seconds since 1970-01-01 00:00.
std::uint32_t secondsSince1970(const DateTime &t);

/// Clock readings before this (2024-01-01) count as "time unknown": a clock
/// that was never set starts near 1970.
inline constexpr std::uint32_t EarliestValidSeconds = 1'704'067'200;

/// Fits a straight line through samples of a clock's error against a
/// reference and gives its slope: how fast the clock runs. A constant delay
/// in the samples does not change the slope.
class ClockRateFit
{
public:
    /// At reference time @p atMs the clock read @p errorMs ahead of it.
    void add(std::int64_t atMs, std::int32_t errorMs);
    void clear() { *this = ClockRateFit(); }

    std::uint32_t count() const { return m_count; }
    /// From the first sample to the last.
    std::int64_t spanMs() const { return m_lastMs; }
    /// Parts per billion the clock runs fast (negative: slow); 0 with fewer
    /// than two samples.
    std::int32_t ratePpb() const;
    /// The samples' root-mean-square distance from the line, in ms.
    std::uint32_t scatterMs() const;

private:
    std::uint32_t m_count = 0;
    std::int64_t m_firstMs = 0;
    std::int32_t m_firstErrorMs = 0;
    std::int64_t m_lastMs = 0;
    // Sums over the samples, relative to the first: x in seconds, y in ms.
    double m_sx = 0;
    double m_sy = 0;
    double m_sxx = 0;
    double m_sxy = 0;
    double m_syy = 0;
};

} // namespace dmm::services
