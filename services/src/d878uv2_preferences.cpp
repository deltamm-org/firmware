// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_preferences.h"

#include "dmm/lib/crc32.h"

#include <algorithm>

namespace dmm::services::d878uv2 {

namespace {

constexpr std::uint8_t Mark[] = {'D', 'M', 'M', 'R'};
constexpr std::uint8_t Version = 1;
constexpr std::size_t VersionAt = 4;
constexpr std::size_t LengthAt = 5;
constexpr std::size_t FieldsAt = 6;
constexpr std::size_t CrcAt = PreferencesRecordBytes - 4;
static_assert(CrcAt - FieldsAt == PreferencesFieldBytes);

// The fields, in record order. A new field goes after the last one and
// moves FieldsEnd past it; no field ever moves or changes its meaning.
constexpr std::size_t FieldsEnd = FieldsAt;
static_assert(FieldsEnd <= CrcAt);

std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

std::optional<RadioPreferences> decodePreferences(std::span<const std::uint8_t, PreferencesRecordBytes> record)
{
    if (!std::equal(std::begin(Mark), std::end(Mark), record.begin()) || record[VersionAt] != Version ||
        record[LengthAt] > PreferencesFieldBytes || lib::crc32(record.data(), CrcAt) != le32(&record[CrcAt])) {
        return std::nullopt;
    }
    return RadioPreferences{};
}

void encodePreferences(const RadioPreferences &, std::span<std::uint8_t, PreferencesRecordBytes> record)
{
    std::fill(record.begin(), record.end(), std::uint8_t{0xFF});
    std::copy(std::begin(Mark), std::end(Mark), record.begin());
    record[VersionAt] = Version;
    record[LengthAt] = static_cast<std::uint8_t>(FieldsEnd - FieldsAt);
    const std::uint32_t crc = lib::crc32(record.data(), CrcAt);
    for (std::size_t i = 0; i < 4; ++i) {
        record[CrcAt + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
}

} // namespace dmm::services::d878uv2
