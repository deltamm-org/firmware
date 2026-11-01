// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// Images the loader handles, where they go, and the hand-over block an
// image leaves for the loader across a restart.

#include "dmm/services/boot_flash.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services::boot {

/// Where the loader keeps things in program flash, and the RAM an image's
/// stack must start in. Every area is whole sectors. Images are always
/// linked for slot A; slot B holds a download or the previous image.
struct BootLayout
{
    FlashArea slotA;
    FlashArea slotB;
    /// The boot record: two sectors, one in use at a time.
    std::uint32_t record[2];
    FlashArea ram;
    /// Offset of the image info block from an image's start.
    std::uint32_t infoOffset;
};

inline constexpr std::uint32_t sectorsFor(std::uint32_t bytes)
{
    return (bytes + SectorBytes - 1) / SectorBytes;
}

/// "DMMI" in memory order.
inline constexpr std::uint32_t ImageInfoMagic = 0x494D4D44;
inline constexpr std::size_t ImageVersionBytes = 24;

enum class ImageKind : std::uint32_t
{
    App = 1,
    Loader = 2,
    Stub = 3,
};

/// A constant every image carries right after its vector table.
struct ImageInfo
{
    std::uint32_t magic = 0;
    ImageKind kind = ImageKind::App;
    /// The address the image is linked for.
    std::uint32_t base = 0;
    /// The first address after the image.
    std::uint32_t end = 0;
    /// The version, NUL-padded.
    char version[ImageVersionBytes] = {};
};
inline constexpr std::size_t ImageInfoBytes = 16 + ImageVersionBytes;

/// Decodes an image info block from its little-endian bytes. False when
/// it is too short or its magic is wrong.
bool parseImageInfo(std::span<const std::uint8_t> bytes, ImageInfo &info);

enum class ImageProblem : std::uint8_t
{
    None,
    /// Shorter than its vector table and info block, or longer than a slot.
    BadLength,
    /// The initial stack pointer is not in RAM.
    BadStack,
    /// The reset handler is not inside the image.
    BadEntry,
    /// No image info block, or not an app.
    NoInfo,
    /// Linked for another address.
    WrongBase,
    /// The info block names another length.
    WrongLength,
    /// The CRC-32 does not match.
    BadCrc,
    /// The flash could not be read.
    ReadError,
};

/// Checks the app image of @p length bytes stored at @p at: vector table,
/// info block (linked for slot A, @p length long) and, with @p checkCrc,
/// its CRC-32 against @p crc.
ImageProblem checkImage(ProgramFlash &flash, const BootLayout &layout, std::uint32_t at, std::uint32_t length,
    bool checkCrc, std::uint32_t crc);

/// The CRC-32 (as zlib) of @p length bytes of flash at @p at. False if
/// the flash could not be read.
bool flashCrc(ProgramFlash &flash, std::uint32_t at, std::uint32_t length, std::uint32_t &crc);

/// What an image asks the loader for across a restart.
enum class HandoverRequest : std::uint32_t
{
    None = 0,
    /// Stay in loader mode and wait for the PC.
    Stay = 1,
    /// Bring the previous image back.
    Rollback = 2,
    /// Test: restart after the given number of swap steps.
    SwapCut = 3,
};

/// Sixteen bytes at a fixed RAM address that survive a restart. Trusted
/// only after a software or watchdog reset, and cleared once read.
struct Handover
{
    std::uint32_t magic;
    std::uint32_t request;
    std::uint32_t argument;
    std::uint32_t check;
};
static_assert(sizeof(Handover) == 16);

/// "DMLD" in memory order.
inline constexpr std::uint32_t HandoverMagic = 0x444C4D44;

inline constexpr Handover makeHandover(HandoverRequest request, std::uint32_t argument)
{
    const auto r = static_cast<std::uint32_t>(request);
    return {HandoverMagic, r, argument, ~(HandoverMagic ^ r ^ argument)};
}

/// The request in @p h, or None when it is not a valid block.
inline constexpr HandoverRequest readHandover(const Handover &h, std::uint32_t &argument)
{
    if (h.magic != HandoverMagic || h.check != ~(h.magic ^ h.request ^ h.argument) || h.request > 3) {
        return HandoverRequest::None;
    }
    argument = h.argument;
    return static_cast<HandoverRequest>(h.request);
}

} // namespace dmm::services::boot
