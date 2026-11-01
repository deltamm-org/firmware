// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/boot_image.h"

#include "dmm/lib/crc32.h"

namespace dmm::services::boot {

namespace {

std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

bool parseImageInfo(std::span<const std::uint8_t> bytes, ImageInfo &info)
{
    if (bytes.size() < ImageInfoBytes || le32(bytes.data()) != ImageInfoMagic) {
        return false;
    }
    info.magic = ImageInfoMagic;
    info.kind = static_cast<ImageKind>(le32(bytes.data() + 4));
    info.base = le32(bytes.data() + 8);
    info.end = le32(bytes.data() + 12);
    for (std::size_t i = 0; i < ImageVersionBytes; ++i) {
        info.version[i] = static_cast<char>(bytes[16 + i]);
    }
    info.version[ImageVersionBytes - 1] = '\0';
    return true;
}

bool flashCrc(ProgramFlash &flash, std::uint32_t at, std::uint32_t length, std::uint32_t &crc)
{
    std::uint8_t chunk[256];
    std::uint32_t c = 0;
    for (std::uint32_t done = 0; done < length;) {
        const std::uint32_t n = length - done < sizeof chunk ? length - done : static_cast<std::uint32_t>(sizeof chunk);
        if (!flash.read(at + done, std::span(chunk, n))) {
            return false;
        }
        c = lib::crc32(chunk, n, c);
        done += n;
    }
    crc = c;
    return true;
}

ImageProblem checkImage(ProgramFlash &flash, const BootLayout &layout, std::uint32_t at, std::uint32_t length,
    bool checkCrc, std::uint32_t crc)
{
    const std::uint32_t capacity = layout.slotB.end - layout.slotB.begin;
    if (length < layout.infoOffset + ImageInfoBytes || length > capacity) {
        return ImageProblem::BadLength;
    }
    std::uint8_t vectors[8];
    std::uint8_t infoBytes[ImageInfoBytes];
    if (!flash.read(at, vectors) || !flash.read(at + layout.infoOffset, infoBytes)) {
        return ImageProblem::ReadError;
    }
    const std::uint32_t stack = le32(vectors);
    const std::uint32_t entry = le32(vectors + 4);
    if (stack % 4 != 0 || stack <= layout.ram.begin || stack > layout.ram.end) {
        return ImageProblem::BadStack;
    }
    const std::uint32_t base = layout.slotA.begin;
    if ((entry & 1u) == 0 || (entry & ~1u) < base + layout.infoOffset || (entry & ~1u) >= base + length) {
        return ImageProblem::BadEntry;
    }
    ImageInfo info;
    if (!parseImageInfo(infoBytes, info) || info.kind != ImageKind::App) {
        return ImageProblem::NoInfo;
    }
    if (info.base != base) {
        return ImageProblem::WrongBase;
    }
    if (info.end - info.base != length) {
        return ImageProblem::WrongLength;
    }
    if (checkCrc) {
        std::uint32_t actual = 0;
        if (!flashCrc(flash, at, length, actual)) {
            return ImageProblem::ReadError;
        }
        if (actual != crc) {
            return ImageProblem::BadCrc;
        }
    }
    return ImageProblem::None;
}

} // namespace dmm::services::boot
