// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_preferences.h"

#include "dmm/lib/crc32.h"
#include "dmm/services/microphone_gain.h"

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

// The home screen's layout: top line, clock (0 off, 1 on), large name,
// middle row, the reading next to the power, bottom row; each a choice of
// HomeLayout's in its order.
constexpr std::size_t HomeTopAt = FieldsAt + 0;
constexpr std::size_t HomeClockAt = FieldsAt + 1;
constexpr std::size_t HomeBigAt = FieldsAt + 2;
constexpr std::size_t HomeMiddleAt = FieldsAt + 3;
constexpr std::size_t HomeBesideAt = FieldsAt + 4;
constexpr std::size_t HomeBottomAt = FieldsAt + 5;
// Microphone gain on DMR and on FM, in dB; 0xFF follows the codeplug.
constexpr std::size_t DmrMicGainAt = FieldsAt + 6;
constexpr std::size_t FmMicGainAt = FieldsAt + 7;
constexpr std::size_t FieldsEnd = FieldsAt + 8;
static_assert(FieldsEnd <= CrcAt);

// The field bytes a record holds; a field past them reads as its default.
class Fields
{
public:
    explicit Fields(std::span<const std::uint8_t, PreferencesRecordBytes> record)
        : m_record(record), m_end(FieldsAt + record[LengthAt])
    {
    }

    std::optional<std::uint8_t> byte(std::size_t at) const
    {
        return at < m_end ? std::optional<std::uint8_t>(m_record[at]) : std::nullopt;
    }
    /// 0 or 1; anything else gives @p fallback.
    bool flag(std::size_t at, bool fallback) const
    {
        const auto b = byte(at);
        return b && *b <= 1 ? *b == 1 : fallback;
    }
    /// One of @p E's values below E::Count; anything else gives @p fallback.
    template <class E>
    E choice(std::size_t at, E fallback) const
    {
        const auto b = byte(at);
        return b && *b < static_cast<std::uint8_t>(E::Count) ? static_cast<E>(*b) : fallback;
    }
    /// 0 to @p max; anything else gives none.
    std::optional<std::uint8_t> upTo(std::size_t at, int max) const
    {
        const auto b = byte(at);
        return b && *b <= max ? b : std::nullopt;
    }

private:
    std::span<const std::uint8_t, PreferencesRecordBytes> m_record;
    std::size_t m_end;
};

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
    const Fields f(record);
    const RadioPreferences d;
    RadioPreferences p;
    p.home.top = f.choice(HomeTopAt, d.home.top);
    p.home.clock = f.flag(HomeClockAt, d.home.clock);
    p.home.big = f.choice(HomeBigAt, d.home.big);
    p.home.middle = f.choice(HomeMiddleAt, d.home.middle);
    p.home.beside = f.choice(HomeBesideAt, d.home.beside);
    p.home.bottom = f.choice(HomeBottomAt, d.home.bottom);
    p.dmrMicGainDb = f.upTo(DmrMicGainAt, microphone_gain::MaxDigitalDb);
    p.fmMicGainDb = f.upTo(FmMicGainAt, microphone_gain::MaxFmDb);
    return p;
}

void encodePreferences(const RadioPreferences &p, std::span<std::uint8_t, PreferencesRecordBytes> record)
{
    std::fill(record.begin(), record.end(), std::uint8_t{0xFF});
    std::copy(std::begin(Mark), std::end(Mark), record.begin());
    record[VersionAt] = Version;
    record[LengthAt] = static_cast<std::uint8_t>(FieldsEnd - FieldsAt);
    record[HomeTopAt] = static_cast<std::uint8_t>(p.home.top);
    record[HomeClockAt] = p.home.clock ? 1 : 0;
    record[HomeBigAt] = static_cast<std::uint8_t>(p.home.big);
    record[HomeMiddleAt] = static_cast<std::uint8_t>(p.home.middle);
    record[HomeBesideAt] = static_cast<std::uint8_t>(p.home.beside);
    record[HomeBottomAt] = static_cast<std::uint8_t>(p.home.bottom);
    record[DmrMicGainAt] = p.dmrMicGainDb.value_or(0xFF);
    record[FmMicGainAt] = p.fmMicGainDb.value_or(0xFF);
    const std::uint32_t crc = lib::crc32(record.data(), CrcAt);
    for (std::size_t i = 0; i < 4; ++i) {
        record[CrcAt + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
}

} // namespace dmm::services::d878uv2
