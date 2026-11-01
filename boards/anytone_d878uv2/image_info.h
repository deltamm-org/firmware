// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The image info block each of the radio's images carries right after its
// vector table (services/boot_image.h), filled in by the compiler and the
// linker: no step after the build.

#include "dmm/services/boot_image.h"
#include "dmm/version.h"

#include <array>
#include <cstdint>
#include <string_view>

extern "C" {
extern const void *const vectorTable[];
extern const std::uint8_t __image_end[];
}

namespace board {

struct FirmwareImageInfo
{
    std::uint32_t magic;
    dmm::services::boot::ImageKind kind;
    const void *base;
    const void *end;
    std::array<char, dmm::services::boot::ImageVersionBytes> version;
};
static_assert(sizeof(FirmwareImageInfo) == dmm::services::boot::ImageInfoBytes);

constexpr std::array<char, dmm::services::boot::ImageVersionBytes> versionField(std::string_view text)
{
    std::array<char, dmm::services::boot::ImageVersionBytes> field{};
    for (std::size_t i = 0; i < text.size() && i + 1 < field.size(); ++i) {
        field[i] = text[i];
    }
    return field;
}

constexpr FirmwareImageInfo imageInfo(dmm::services::boot::ImageKind kind)
{
    return {dmm::services::boot::ImageInfoMagic, kind, vectorTable, __image_end, versionField(dmm::Version)};
}

} // namespace board

/// Defines the image info block of an image of @p kind.
#define DMM_IMAGE_INFO(kind) \
    [[gnu::section(".image_info"), gnu::used]] constinit const board::FirmwareImageInfo dmmImageInfo = board::imageInfo(kind)
