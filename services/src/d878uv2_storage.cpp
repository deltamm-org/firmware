// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_storage.h"

namespace dmm::services {

namespace {

using drivers::storage::SpiNand;

constexpr int RecordSlots = 125;
constexpr std::uint32_t RecordStep = 0x400;
constexpr std::uint32_t RecordBytes = 0x1F8;
constexpr std::uint32_t MarkerAt = 0x1F4;
constexpr std::uint32_t ReplacementsAt = 0xFA;
constexpr std::uint32_t CountAt = 0x1F6;
constexpr std::uint8_t RecordMarker = 0x55;

StorageResult fromNand(SpiNand::Result r)
{
    switch (r) {
    case SpiNand::Result::Ok:
        return StorageResult::Ok;
    case SpiNand::Result::Corrected:
        return StorageResult::Corrected;
    case SpiNand::Result::Uncorrectable:
        return StorageResult::Uncorrectable;
    case SpiNand::Result::Timeout:
        break;
    }
    return StorageResult::Timeout;
}

// Keeps the worse of two results.
StorageResult worse(StorageResult a, StorageResult b)
{
    return static_cast<int>(b) > static_cast<int>(a) ? b : a;
}

std::uint16_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

const char *resultName(StorageResult r)
{
    switch (r) {
    case StorageResult::Ok:
        return "ok";
    case StorageResult::Corrected:
        return "corrected";
    case StorageResult::Uncorrectable:
        return "uncorrectable";
    case StorageResult::Timeout:
        return "timeout";
    case StorageResult::NoChip:
        return "no chip";
    case StorageResult::OutOfRange:
        return "out of range";
    }
    return "?";
}

D878Uv2Storage::D878Uv2Storage(SpiNand &nand)
    : m_nand(nand)
{
}

void D878Uv2Storage::start()
{
    m_lock.lock();
    m_present = m_nand.identify();
    if (m_present) {
        loadRemapTable();
    }
    m_lock.unlock();
}

void D878Uv2Storage::loadRemapTable()
{
    m_entries = -1;
    int newest = -1;
    for (int slot = 0; slot < RecordSlots; ++slot) {
        std::uint8_t marker = 0;
        if (!usable(readPhysical(slot * RecordStep + MarkerAt, std::span(&marker, 1))) || marker != RecordMarker) {
            break;
        }
        newest = slot;
    }
    if (newest < 0) {
        return;
    }
    std::uint8_t record[RecordBytes];
    if (!usable(readPhysical(static_cast<std::uint32_t>(newest) * RecordStep, record))) {
        return;
    }
    const int count = record[CountAt];
    if (count > MaxRemapEntries) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        m_bad[i] = le16(record + 2 * i);
        m_replacement[i] = le16(record + ReplacementsAt + 2 * i);
    }
    m_entries = count;
}

std::uint16_t D878Uv2Storage::remapBlock(std::uint16_t block) const
{
    for (int i = 0; i < m_entries; ++i) {
        if (m_bad[i] == block) {
            return m_replacement[i];
        }
    }
    return block;
}

StorageInfo D878Uv2Storage::info()
{
    m_lock.lock();
    StorageInfo i;
    i.present = m_present;
    i.maker = m_nand.id().maker;
    i.device = m_nand.id().device;
    i.sizeBytes = m_nand.pageCount() * SpiNand::PageBytes;
    i.remappedBlocks = m_entries;
    m_lock.unlock();
    return i;
}

StorageResult D878Uv2Storage::readPhysical(std::uint32_t address, std::span<std::uint8_t> data)
{
    StorageResult result = StorageResult::Ok;
    std::size_t done = 0;
    while (done < data.size()) {
        const std::uint32_t at = address + static_cast<std::uint32_t>(done);
        const auto column = static_cast<std::uint16_t>(at % SpiNand::PageBytes);
        std::size_t n = SpiNand::PageBytes - column;
        if (n > data.size() - done) {
            n = data.size() - done;
        }
        const StorageResult r = fromNand(m_nand.read(at / SpiNand::PageBytes, column, data.data() + done, n));
        if (r == StorageResult::Timeout) {
            return r;
        }
        result = worse(result, r);
        done += n;
    }
    return result;
}

StorageResult D878Uv2Storage::read(std::uint32_t address, std::span<std::uint8_t> data, bool physical)
{
    m_lock.lock();
    StorageResult result = StorageResult::Ok;
    const std::uint32_t size = m_nand.pageCount() * SpiNand::PageBytes;
    if (!m_present) {
        result = StorageResult::NoChip;
    } else if (address >= size || data.size() > size - address) {
        result = StorageResult::OutOfRange;
    } else {
        // Each 128 KB block may be remapped on its own.
        std::size_t done = 0;
        while (done < data.size() && result != StorageResult::Timeout) {
            const std::uint32_t at = address + static_cast<std::uint32_t>(done);
            std::size_t n = BlockBytes - at % BlockBytes;
            if (n > data.size() - done) {
                n = data.size() - done;
            }
            std::uint32_t target = at;
            if (!physical) {
                target = static_cast<std::uint32_t>(remapBlock(static_cast<std::uint16_t>(at / BlockBytes))) * BlockBytes +
                    at % BlockBytes;
            }
            result = worse(result, readPhysical(target, data.subspan(done, n)));
            done += n;
        }
    }
    m_lock.unlock();
    return result;
}

} // namespace dmm::services
