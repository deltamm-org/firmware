// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/clock.h"

namespace dmm::services {

namespace {

constexpr std::uint32_t SecondsPerDay = 86'400;

// Days from 1970-01-01 to the date, and back: the proleptic Gregorian
// calendar counted in 400-year eras from March, so leap days fall at the
// end of each counted year.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146'097 + static_cast<std::int64_t>(doe) - 719'468;
}

} // namespace

DateTime dateTime(std::uint32_t seconds)
{
    const std::int64_t z = seconds / SecondsPerDay + 719'468;
    const std::int64_t era = z / 146'097;
    const auto doe = static_cast<unsigned>(z - era * 146'097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year = static_cast<std::int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
    const std::uint32_t inDay = seconds % SecondsPerDay;
    DateTime t;
    t.year = static_cast<std::uint16_t>(year);
    t.month = static_cast<std::uint8_t>(month);
    t.day = static_cast<std::uint8_t>(day);
    t.hour = static_cast<std::uint8_t>(inDay / 3600);
    t.minute = static_cast<std::uint8_t>(inDay / 60 % 60);
    t.second = static_cast<std::uint8_t>(inDay % 60);
    return t;
}

void ClockRateFit::add(std::int64_t atMs, std::int32_t errorMs)
{
    if (m_count == 0) {
        m_firstMs = atMs;
        m_firstErrorMs = errorMs;
    }
    ++m_count;
    m_lastMs = atMs - m_firstMs;
    const double x = static_cast<double>(m_lastMs) / 1000;
    const double y = errorMs - m_firstErrorMs;
    m_sx += x;
    m_sy += y;
    m_sxx += x * x;
    m_sxy += x * y;
    m_syy += y * y;
}

std::int32_t ClockRateFit::ratePpb() const
{
    const double n = m_count;
    const double sxx = m_sxx - m_sx * m_sx / n;
    if (m_count < 2 || sxx <= 0) {
        return 0;
    }
    // Slope in ms per second, which is parts per thousand.
    const double slope = (m_sxy - m_sx * m_sy / n) / sxx;
    const double ppb = slope * 1'000'000;
    return static_cast<std::int32_t>(ppb < 0 ? ppb - 0.5 : ppb + 0.5);
}

std::uint32_t ClockRateFit::scatterMs() const
{
    if (m_count < 3) {
        return 0;
    }
    const double n = m_count;
    const double sxx = m_sxx - m_sx * m_sx / n;
    const double sxy = m_sxy - m_sx * m_sy / n;
    const double syy = m_syy - m_sy * m_sy / n;
    const double residual = sxx > 0 ? syy - sxy * sxy / sxx : syy;
    const double meanSquare = residual > 0 ? residual / n : 0;
    // Square root by Newton's method; the library's sqrt may set errno.
    double r = meanSquare > 1 ? meanSquare : 1;
    for (int i = 0; i < 40; ++i) {
        r = (r + meanSquare / r) / 2;
    }
    return static_cast<std::uint32_t>(r + 0.5);
}

std::uint32_t secondsSince1970(const DateTime &t)
{
    const std::int64_t days = daysFromCivil(t.year, t.month, t.day);
    return static_cast<std::uint32_t>(days * SecondsPerDay + t.hour * 3600 + t.minute * 60 + t.second);
}

} // namespace dmm::services
