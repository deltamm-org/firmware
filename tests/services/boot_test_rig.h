// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "fake_program_flash.h"

#include "dmm/lib/crc32.h"
#include "dmm/services/boot_loader.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace boot_test {

using namespace dmm::services::boot;

/// A small layout with the real page sizes: slot B crosses from 2 KB pages
/// into 4 KB pages. The fake flash spans it with protected margins around.
inline constexpr std::uint32_t SmallFlashBase = 0x08074000;
inline constexpr std::uint32_t SmallFlashSize = 0x14000;
/// Slot A 5 sectors, slot B 4 sectors (across 0x08080000), record after.
inline constexpr BootLayout Small{
    {0x0807A000, 0x0807F000},
    {0x0807F000, 0x08083000},
    {0x08083000, 0x08084000},
    {0x20000000, 0x20017000},
    0x150,
};

inline std::vector<std::uint8_t> makeImage(const BootLayout &layout, std::uint32_t length, std::uint32_t seed,
    std::string_view version = "1.0")
{
    std::vector<std::uint8_t> b(length);
    std::uint32_t x = seed * 2654435761u + 1;
    for (auto &v : b) {
        x = x * 1103515245u + 12345u;
        v = static_cast<std::uint8_t>(x >> 16);
    }
    auto put = [&](std::uint32_t at, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            b[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    };
    const std::uint32_t base = layout.slotA.begin;
    put(0, layout.ram.end - 0x100);
    put(4, (base + layout.infoOffset + ImageInfoBytes) | 1u);
    put(layout.infoOffset, ImageInfoMagic);
    put(layout.infoOffset + 4, static_cast<std::uint32_t>(ImageKind::App));
    put(layout.infoOffset + 8, base);
    put(layout.infoOffset + 12, base + length);
    for (std::size_t i = 0; i < ImageVersionBytes; ++i) {
        b[layout.infoOffset + 16 + i] = i < version.size() ? static_cast<std::uint8_t>(version[i]) : 0;
    }
    return b;
}

inline std::uint32_t crcOf(const std::vector<std::uint8_t> &b)
{
    return dmm::lib::crc32(b.data(), b.size());
}

class NoEvents final : public LoaderEvents
{
public:
    void busy(bool on) override { busyNow = on; }
    void alive() override { ++alives; }
    bool busyNow = false;
    unsigned alives = 0;
};

/// A radio: fake flash, the guard, and a fresh loader for every start.
struct Rig
{
    explicit Rig(const BootLayout &l = Small, std::uint32_t flashBase = SmallFlashBase, std::uint32_t flashSize = SmallFlashSize)
        : layout(l), raw(flashBase, flashSize, 0x08080000), protectedBase(flashBase),
          writable{{l.slotA.begin, l.slotA.end}, {l.slotB.begin, l.slotB.end}, {l.record[0], l.record[1] + SectorBytes}},
          guarded(raw, writable)
    {
        // A pattern outside the writable areas, checked by untouched().
        for (std::size_t i = 0; i < raw.bytes().size(); ++i) {
            raw.bytes()[i] = static_cast<std::uint8_t>(0xA5 ^ (i * 7));
        }
        for (const FlashArea &a : writable) {
            std::memset(raw.at(a.begin), 0xFF, a.end - a.begin);
        }
        protectedCopy = raw.bytes();
    }

    BootResult boot(const BootInputs &in = {})
    {
        Loader loader(guarded, layout, buffer, events);
        return loader.boot(in);
    }

    /// Starts again and again while the loader asks for a restart. Stops
    /// at a start of the image, loader mode, or a power loss.
    BootResult settle(const BootInputs &first = {}, int limit = 40)
    {
        BootResult r = boot(first);
        for (int i = 0; i < limit && r.outcome == Outcome::Reset && raw.powered(); ++i) {
            r = boot();
        }
        return r;
    }

    BootState state()
    {
        BootRecord record(guarded, layout.record);
        record.load();
        return record.state();
    }

    bool recordValid()
    {
        BootRecord record(guarded, layout.record);
        return record.load();
    }

    void putInSlot(bool slotB, const std::vector<std::uint8_t> &image)
    {
        const std::uint32_t at = slotB ? layout.slotB.begin : layout.slotA.begin;
        std::memcpy(raw.at(at), image.data(), image.size());
    }

    bool slotHolds(bool slotB, const std::vector<std::uint8_t> &image)
    {
        const std::uint32_t at = slotB ? layout.slotB.begin : layout.slotA.begin;
        return std::memcmp(raw.at(at), image.data(), image.size()) == 0;
    }

    /// Commits @p image in slot B as the PC would, after an erase.
    bool commit(const std::vector<std::uint8_t> &image, ConfirmMode mode = ConfirmMode::Automatic)
    {
        Loader loader(guarded, layout, buffer, events);
        if (!loader.record().load() || !loader.eraseDownload(static_cast<std::uint32_t>(image.size()))) {
            return false;
        }
        if (!guarded.program(layout.slotB.begin, image)) {
            return false;
        }
        const SlotRecord c{SlotState::Committed, mode, static_cast<std::uint32_t>(image.size()), crcOf(image)};
        return loader.record().reserve(1) && loader.record().append(entry::slot(true, c));
    }

    /// The running image confirms itself.
    bool confirm()
    {
        const FlashArea recordOnly[] = {{layout.record[0], layout.record[1] + SectorBytes}};
        GuardedFlash imageFlash(raw, recordOnly);
        BootRecord record(imageFlash, layout.record);
        if (!record.load() || record.state().a.state != SlotState::Trial) {
            return false;
        }
        return record.append(entry::confirm(record.state().a.crc));
    }

    /// Bytes outside the writable areas are as they were.
    bool untouched() const
    {
        const auto &now = raw.bytes();
        std::uint32_t from = base();
        const std::uint32_t end = base() + static_cast<std::uint32_t>(now.size());
        // The writable areas are in address order.
        for (std::size_t i = 0; i <= std::size(writable); ++i) {
            const std::uint32_t to = i < std::size(writable) ? writable[i].begin : end;
            if (to > from && std::memcmp(now.data() + (from - base()), protectedCopy.data() + (from - base()), to - from) != 0) {
                return false;
            }
            if (i < std::size(writable)) {
                from = writable[i].end;
            }
        }
        return true;
    }

    std::uint32_t base() const { return protectedBase; }

    const BootLayout &layout;
    FakeProgramFlash raw;
    std::uint32_t protectedBase;
    FlashArea writable[3];
    GuardedFlash guarded;
    std::array<std::uint8_t, SectorBytes> buffer{};
    NoEvents events;
    std::vector<std::uint8_t> protectedCopy;
};

} // namespace boot_test
