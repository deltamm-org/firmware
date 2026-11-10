// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/codeplug.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services::d878uv2 {

/// DeltaMM's preferences record in the D878UVII's codeplug:
///   0   the mark "DMMR"
///   4   the format version, 1
///   5   how many field bytes follow
///   6   the fields, each at a fixed place; unused bytes 0xFF
///   end CRC-32 of everything before it, little-endian, in the last four
///       bytes
/// Fields are only ever added after the last one, so a record from before
/// a field holds fewer field bytes and that field reads as its default. A
/// value out of range also reads as the default. A record with another
/// mark, version or checksum counts as none.
inline constexpr std::size_t PreferencesRecordBytes = 0x80;
/// Room for fields: the record less its header and checksum.
inline constexpr std::size_t PreferencesFieldBytes = PreferencesRecordBytes - 6 - 4;

/// The record's preferences; empty when it holds none.
std::optional<RadioPreferences> decodePreferences(std::span<const std::uint8_t, PreferencesRecordBytes> record);

/// @p record filled with @p p.
void encodePreferences(const RadioPreferences &p, std::span<std::uint8_t, PreferencesRecordBytes> record);

} // namespace dmm::services::d878uv2
