// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/boot_record.h"

namespace dmm::services::boot {

namespace {

constexpr std::uint32_t FormatVersion = 1;
/// "DMBR" in memory order.
constexpr std::uint32_t HeaderMagic = 0x52424D44;
constexpr std::uint32_t Erased = 0xFFFFFFFFu;

std::uint32_t firstWord(const Entry &e)
{
    return static_cast<std::uint32_t>(e.kind) << 24 | (e.a & 0xFFFFFFu);
}

void encode(const Entry &e, std::uint8_t *out)
{
    const std::uint32_t w0 = firstWord(e);
    const std::uint32_t words[4] = {w0, e.b, e.c, entryCheck(w0, e.b, e.c)};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            out[4 * i + j] = static_cast<std::uint8_t>(words[i] >> (8 * j));
        }
    }
}

bool decode(const std::uint32_t (&w)[4], Entry &e)
{
    if (w[3] != entryCheck(w[0], w[1], w[2])) {
        return false;
    }
    e.kind = static_cast<EntryKind>(w[0] >> 24);
    e.a = w[0] & 0xFFFFFFu;
    e.b = w[1];
    e.c = w[2];
    return true;
}

Entry header(std::uint32_t generation)
{
    return {EntryKind::Header, FormatVersion, generation, HeaderMagic};
}

SlotRecord &slotOf(BootState &s, bool slotB)
{
    return slotB ? s.b : s.a;
}

} // namespace

std::uint32_t entryCheck(std::uint32_t w0, std::uint32_t w1, std::uint32_t w2)
{
    std::uint32_t h = 0x5A17C0DEu;
    for (const std::uint32_t w : {w0, w1, w2}) {
        h = (h ^ w) * 0x9E3779B1u;
        h ^= h >> 15;
    }
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h == 0 || h == Erased ? 0x6D5A4C3Bu : h;
}

namespace entry {

Entry slot(bool slotB, const SlotRecord &record)
{
    return {EntryKind::Slot,
        (slotB ? 1u : 0u) << 16 | static_cast<std::uint32_t>(record.state) << 8 | static_cast<std::uint32_t>(record.mode),
        record.length, record.crc};
}

Entry attempt(std::uint8_t count)
{
    return {EntryKind::Attempt, count};
}

Entry confirm(std::uint32_t crc)
{
    return {EntryKind::Confirm, 0, crc};
}

Entry swapBegin(SwapKind kind, Fallback reason, std::uint16_t sectors)
{
    return {EntryKind::SwapBegin, static_cast<std::uint32_t>(kind) << 16 | static_cast<std::uint32_t>(reason) << 8, sectors};
}

Entry swapStep(std::uint16_t step)
{
    return {EntryKind::SwapStep, step};
}

Entry swapEnd()
{
    return {EntryKind::SwapEnd};
}

Entry fallback(Fallback reason)
{
    return {EntryKind::Fallback, static_cast<std::uint32_t>(reason)};
}

} // namespace entry

void apply(BootState &s, const Entry &e)
{
    switch (e.kind) {
    case EntryKind::Slot: {
        SlotRecord &slot = slotOf(s, ((e.a >> 16) & 0xFFu) != 0);
        slot.state = static_cast<SlotState>((e.a >> 8) & 0xFFu);
        slot.mode = static_cast<ConfirmMode>(e.a & 0xFFu);
        slot.length = e.b;
        slot.crc = e.c;
        break;
    }
    case EntryKind::Attempt:
        s.attempts = static_cast<std::uint8_t>(e.a);
        break;
    case EntryKind::Confirm:
        if (s.a.state == SlotState::Trial && s.a.crc == e.b) {
            s.a.state = SlotState::Confirmed;
        }
        break;
    case EntryKind::SwapBegin:
        s.swap.active = true;
        s.swap.kind = static_cast<SwapKind>((e.a >> 16) & 0xFFu);
        s.swap.reason = static_cast<Fallback>((e.a >> 8) & 0xFFu);
        s.swap.sectors = static_cast<std::uint16_t>(e.b);
        s.swap.done = 0;
        break;
    case EntryKind::SwapStep:
        if (s.swap.active) {
            s.swap.done = static_cast<std::uint16_t>(e.a + 1);
        }
        break;
    case EntryKind::SwapEnd: {
        if (!s.swap.active) {
            break;
        }
        const SlotRecord oldA = s.a;
        const SlotRecord oldB = s.b;
        s.a = oldB;
        s.b = oldA;
        switch (s.swap.kind) {
        case SwapKind::Install:
            s.a.state = SlotState::Trial;
            if (oldA.state != SlotState::Confirmed && oldA.state != SlotState::Empty) {
                s.b.state = SlotState::Failed;
            }
            break;
        case SwapKind::Revert:
            s.a.state = SlotState::Confirmed;
            if (oldA.state != SlotState::Empty) {
                s.b.state = SlotState::Failed;
            }
            s.lastFallback = s.swap.reason;
            break;
        case SwapKind::Rollback:
            s.a.state = SlotState::Confirmed;
            if (oldA.state != SlotState::Confirmed && oldA.state != SlotState::Empty) {
                s.b.state = SlotState::Failed;
            }
            s.lastFallback = Fallback::Requested;
            break;
        }
        s.attempts = 0;
        s.swap = {};
        break;
    }
    case EntryKind::Fallback:
        s.lastFallback = static_cast<Fallback>(e.a);
        break;
    case EntryKind::Header:
        break;
    }
}

BootRecord::BootRecord(ProgramFlash &flash, const std::uint32_t (&sectors)[2])
    : m_flash(flash), m_sectors{sectors[0], sectors[1]}
{
}

bool BootRecord::readEntry(std::uint32_t sector, std::uint32_t slot, std::uint32_t (&words)[4])
{
    std::uint8_t bytes[EntryBytes];
    if (!m_flash.read(sector + slot * EntryBytes, bytes)) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        words[i] = static_cast<std::uint32_t>(bytes[4 * i]) | static_cast<std::uint32_t>(bytes[4 * i + 1]) << 8 |
            static_cast<std::uint32_t>(bytes[4 * i + 2]) << 16 | static_cast<std::uint32_t>(bytes[4 * i + 3]) << 24;
    }
    return true;
}

bool BootRecord::load()
{
    m_current = -1;
    m_generation = 0;
    m_state = {};
    for (int i = 0; i < 2; ++i) {
        std::uint32_t w[4];
        Entry e{};
        if (!readEntry(m_sectors[i], 0, w) || !decode(w, e) || e.kind != EntryKind::Header || e.a != FormatVersion ||
            e.c != HeaderMagic) {
            continue;
        }
        if (m_current < 0 || e.b > m_generation) {
            m_current = i;
            m_generation = e.b;
        }
    }
    if (m_current < 0) {
        return false;
    }
    const std::uint32_t sector = m_sectors[m_current];
    m_next = SlotsPerSector;
    for (std::uint32_t slot = 1; slot < SlotsPerSector; ++slot) {
        std::uint32_t w[4];
        if (!readEntry(sector, slot, w)) {
            m_current = -1;
            m_state = {};
            return false;
        }
        if (w[0] == Erased && w[1] == Erased && w[2] == Erased && w[3] == Erased) {
            m_next = slot;
            break;
        }
        Entry e{};
        if (decode(w, e)) {
            apply(m_state, e);
        }
    }
    return true;
}

std::uint32_t BootRecord::freeEntries() const
{
    return valid() ? SlotsPerSector - m_next : 0;
}

bool BootRecord::append(const Entry &e)
{
    if (!valid()) {
        return false;
    }
    std::uint8_t bytes[EntryBytes];
    encode(e, bytes);
    // A slot that does not take the entry is skipped: it fails its check.
    for (int tries = 0; tries < 2 && m_next < SlotsPerSector; ++tries) {
        const std::uint32_t at = m_sectors[m_current] + m_next * EntryBytes;
        ++m_next;
        std::uint8_t back[EntryBytes];
        if (m_flash.program(at, bytes) && m_flash.read(at, back)) {
            bool same = true;
            for (std::size_t i = 0; i < EntryBytes; ++i) {
                same = same && back[i] == bytes[i];
            }
            if (same) {
                apply(m_state, e);
                return true;
            }
        }
    }
    return false;
}

bool BootRecord::rewrite(const BootState &state)
{
    const int target = m_current == 0 ? 1 : 0;
    const std::uint32_t sector = m_sectors[target];
    const std::uint32_t generation = m_generation + 1;

    Entry entries[8];
    std::size_t count = 0;
    entries[count++] = entry::slot(false, state.a);
    entries[count++] = entry::slot(true, state.b);
    if (state.attempts != 0) {
        entries[count++] = entry::attempt(state.attempts);
    }
    if (state.lastFallback != Fallback::None) {
        entries[count++] = entry::fallback(state.lastFallback);
    }
    if (state.swap.active) {
        entries[count++] = entry::swapBegin(state.swap.kind, state.swap.reason, state.swap.sectors);
        if (state.swap.done != 0) {
            entries[count++] = entry::swapStep(static_cast<std::uint16_t>(state.swap.done - 1));
        }
    }

    std::uint8_t bytes[8 * EntryBytes];
    BootState written;
    for (std::size_t i = 0; i < count; ++i) {
        encode(entries[i], bytes + i * EntryBytes);
        apply(written, entries[i]);
    }
    std::uint8_t head[EntryBytes];
    encode(header(generation), head);
    if (!m_flash.eraseSector(sector) || !m_flash.program(sector + EntryBytes, std::span(bytes, count * EntryBytes)) ||
        !m_flash.program(sector, head)) {
        return false;
    }
    m_current = -1;
    // Read back what is now there, as the next start will.
    if (!load() || m_current != target || !(m_state == written)) {
        return false;
    }
    return true;
}

bool BootRecord::reserve(std::uint32_t count)
{
    if (!valid()) {
        return false;
    }
    if (freeEntries() >= count) {
        return true;
    }
    const BootState copy = m_state;
    return rewrite(copy);
}

} // namespace dmm::services::boot
