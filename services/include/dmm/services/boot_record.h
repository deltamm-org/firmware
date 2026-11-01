// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The boot record: an append-only journal in two flash sectors that holds
// what each slot contains, the trial of a new image and the progress of a
// swap. Entries are 16 bytes: kind and a 24-bit value, two 32-bit values,
// and a check over the three. An entry cut short by a power loss fails its
// check and is skipped. Sector slot 0 holds the header, written last when
// a sector is filled from scratch; of two valid sectors the one with the
// higher generation is in use.

#include "dmm/services/boot_flash.h"

#include <cstddef>
#include <cstdint>

namespace dmm::services::boot {

enum class SlotState : std::uint8_t
{
    Empty,
    /// A good image that has run and confirmed itself.
    Confirmed,
    /// A new image on trial in slot A: started a limited number of times
    /// until it confirms.
    Trial,
    /// An image that failed its trial or its check.
    Failed,
    /// A complete download in slot B, waiting to be swapped in.
    Committed,
};

enum class ConfirmMode : std::uint8_t
{
    /// The image confirms itself once it has run a while.
    Automatic,
    /// Only the PC confirms the image.
    Manual,
};

enum class SwapKind : std::uint8_t
{
    /// A committed download goes in; the old image is kept in slot B.
    Install,
    /// A failed image goes out; the previous one comes back.
    Revert,
    /// On request: the previous image comes back.
    Rollback,
};

/// Why the previous image came back, or why there was none to bring.
enum class Fallback : std::uint8_t
{
    None,
    /// The trial image was started the allowed number of times without
    /// confirming.
    AttemptsUsed,
    /// The image in slot A failed its check.
    CheckFailed,
    /// Asked for.
    Requested,
    /// Slot A held no image.
    NoImage,
};

struct SlotRecord
{
    SlotState state = SlotState::Empty;
    ConfirmMode mode = ConfirmMode::Automatic;
    std::uint32_t length = 0;
    std::uint32_t crc = 0;

    bool operator==(const SlotRecord &) const = default;
};

struct SwapProgress
{
    bool active = false;
    SwapKind kind = SwapKind::Install;
    Fallback reason = Fallback::None;
    /// Sectors swapped: the larger image's.
    std::uint16_t sectors = 0;
    /// Steps done, of 3 * sectors.
    std::uint16_t done = 0;

    bool operator==(const SwapProgress &) const = default;
};

struct BootState
{
    SlotRecord a;
    SlotRecord b;
    /// Starts of the trial image in slot A.
    std::uint8_t attempts = 0;
    SwapProgress swap;
    Fallback lastFallback = Fallback::None;

    bool operator==(const BootState &) const = default;
};

enum class EntryKind : std::uint8_t
{
    Header = 0x01,
    Slot = 0x10,
    Attempt = 0x20,
    Confirm = 0x21,
    SwapBegin = 0x30,
    SwapStep = 0x31,
    SwapEnd = 0x32,
    Fallback = 0x40,
};

struct Entry
{
    EntryKind kind;
    /// 24 bits.
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

namespace entry {
Entry slot(bool slotB, const SlotRecord &record);
Entry attempt(std::uint8_t count);
Entry confirm(std::uint32_t crc);
Entry swapBegin(SwapKind kind, Fallback reason, std::uint16_t sectors);
Entry swapStep(std::uint16_t step);
Entry swapEnd();
Entry fallback(Fallback reason);
} // namespace entry

/// Applies one entry to @p state. A swap's end exchanges the two slots'
/// records and sets their states as its kind says.
void apply(BootState &state, const Entry &e);

/// The check word of an entry's first three words; never 0 or 0xFFFFFFFF.
std::uint32_t entryCheck(std::uint32_t w0, std::uint32_t w1, std::uint32_t w2);

class BootRecord
{
public:
    static constexpr std::uint32_t EntryBytes = 16;
    static constexpr std::uint32_t SlotsPerSector = SectorBytes / EntryBytes;

    /// @p sectors are the record's two sector addresses.
    BootRecord(ProgramFlash &flash, const std::uint32_t (&sectors)[2]);

    /// Reads the record. False when neither sector holds a valid one; the
    /// state is then empty and nothing can be appended until rewrite().
    bool load();

    bool valid() const { return m_current >= 0; }
    const BootState &state() const { return m_state; }
    /// Entries that can still be appended to the sector in use.
    std::uint32_t freeEntries() const;
    /// The sector in use and its generation (for tests and reports).
    std::uint32_t generation() const { return m_generation; }

    /// Appends @p e and applies it. False when the sector is full or the
    /// write failed; the state is then unchanged.
    bool append(const Entry &e);

    /// Writes @p state alone into the other sector, which it erases, and
    /// uses that sector from then on.
    bool rewrite(const BootState &state);

    /// Makes room for @p count entries, rewriting when there is less.
    bool reserve(std::uint32_t count);

private:
    bool readEntry(std::uint32_t sector, std::uint32_t slot, std::uint32_t (&words)[4]);

    ProgramFlash &m_flash;
    std::uint32_t m_sectors[2];
    int m_current = -1;
    std::uint32_t m_generation = 0;
    std::uint32_t m_next = 0;
    BootState m_state;
};

} // namespace dmm::services::boot
