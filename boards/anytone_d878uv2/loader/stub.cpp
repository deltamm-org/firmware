// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

// The loader's stub, at the address the radio's boot loader starts. It
// drives the safety pins low, then starts the loader if the loader's CRC
// is good, else the image in slot A, else blinks the red LED. It is never
// written by anything but the radio's update mode.

#include "../image_info.h"
#include "../pins.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/startup.h"
#include "dmm/lib/crc32.h"
#include "dmm/services/boot_image.h"
#include "dmm/services/d878uv2_boot_layout.h"

#include <cstdint>
#include <span>

DMM_IMAGE_INFO(dmm::services::boot::ImageKind::Stub);

namespace {

namespace hal = dmm::hal::gd32f30x;
namespace cm = dmm::hal::cortex_m;
namespace boot = dmm::services::boot;
namespace layout = dmm::services::d878uv2;

std::uint32_t word(std::uint32_t address)
{
    return *reinterpret_cast<const volatile std::uint32_t *>(address);
}

// The loader's info block names it, and the CRC-32 stored right after its
// end matches.
bool loaderGood()
{
    boot::ImageInfo info;
    const std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t *>(layout::LoaderBase + layout::ImageInfoOffset), boot::ImageInfoBytes);
    if (!boot::parseImageInfo(bytes, info) || info.kind != boot::ImageKind::Loader || info.base != layout::LoaderBase ||
        info.end <= layout::LoaderBase + layout::ImageInfoOffset || info.end > layout::LoaderEnd - 4 || info.end % 4 != 0) {
        return false;
    }
    const auto *start = reinterpret_cast<const std::uint8_t *>(layout::LoaderBase);
    return dmm::lib::crc32(start, info.end - layout::LoaderBase) == word(info.end);
}

// A stack pointer in RAM and a reset handler inside the slot.
bool slotStartable(std::uint32_t base, std::uint32_t end)
{
    const std::uint32_t stack = word(base);
    const std::uint32_t entry = word(base + 4);
    return stack > layout::RamBase && stack <= layout::RamEnd && stack % 4 == 0 && (entry & 1u) != 0 &&
        (entry & ~1u) > base && (entry & ~1u) < end;
}

} // namespace

void hal::boardMain()
{
    for (Port port : board::StartPorts) {
        enablePortClock(port);
    }
    for (Pin pin : board::StartLowPins) {
        write(pin, false);
        configure(pin, PinMode::Output);
    }
    if (loaderGood()) {
        cm::startImageAt(layout::LoaderBase);
    }
    if (slotStartable(layout::SlotABase, layout::SlotAEnd)) {
        cm::startImageAt(layout::SlotABase);
    }
    for (std::uint32_t n = 0;; ++n) {
        write(board::RedLed, (n >> 18) % 2 != 0);
    }
}
