// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The D878UVII's program flash (1 MB from 0x08000000) as DeltaMM's loader
// divides it. The radio's boot loader below StubBase and the radio data
// from RadioDataBase on are never erased or written. The linker scripts
// in boards/anytone_d878uv2 use the same numbers.

#include "dmm/services/boot_image.h"

#include <cstdint>

namespace dmm::services::d878uv2 {

inline constexpr std::uint32_t FlashBase = 0x08000000;
inline constexpr std::uint32_t FlashEnd = 0x08100000;
/// Where the radio's boot loader starts an image.
inline constexpr std::uint32_t StubBase = 0x08004000;
inline constexpr std::uint32_t StubEnd = 0x08005000;
inline constexpr std::uint32_t SlotABase = 0x08005000;
inline constexpr std::uint32_t SlotAEnd = 0x08078000;
inline constexpr std::uint32_t SlotBBase = 0x08078000;
inline constexpr std::uint32_t SlotBEnd = 0x080EA000;
inline constexpr std::uint32_t RecordBase = 0x080EA000;
inline constexpr std::uint32_t RecordEnd = 0x080EC000;
/// Reserved for the baseband program; not used yet.
inline constexpr std::uint32_t BasebandAreaBase = 0x080EC000;
inline constexpr std::uint32_t LoaderBase = 0x080ED000;
inline constexpr std::uint32_t LoaderEnd = 0x080F5000;
/// The radio's own data; its first sector also holds the 2 KB below.
inline constexpr std::uint32_t RadioDataBase = 0x080F5800;

inline constexpr std::uint32_t RamBase = 0x20000000;
/// Top of the RAM an image may use; the boot loader keeps the 4 KB above.
inline constexpr std::uint32_t RamEnd = 0x20017000;
/// The hand-over block: the last 16 bytes kept over a restart.
inline constexpr std::uint32_t HandoverAddress = 0x20016FF0;

/// The image info block follows the 84-entry vector table.
inline constexpr std::uint32_t ImageInfoOffset = 84 * 4;

inline constexpr services::boot::BootLayout BootLayout{
    {SlotABase, SlotAEnd},
    {SlotBBase, SlotBEnd},
    {RecordBase, RecordBase + services::boot::SectorBytes},
    {RamBase, RamEnd},
    ImageInfoOffset,
};

/// The areas the loader may erase and program.
inline constexpr services::boot::FlashArea LoaderWritable[] = {{SlotABase, SlotAEnd}, {SlotBBase, SlotBEnd}, {RecordBase, RecordEnd}};
/// The areas a running image may program (it never erases).
inline constexpr services::boot::FlashArea ImageWritable[] = {{RecordBase, RecordEnd}};

static_assert(SlotAEnd - SlotABase == SlotBEnd - SlotBBase + services::boot::SectorBytes,
    "slot A has one sector more than slot B, for the swap");
static_assert(LoaderEnd <= RadioDataBase - 0x800, "the loader keeps out of the radio data's sector");

} // namespace dmm::services::d878uv2
