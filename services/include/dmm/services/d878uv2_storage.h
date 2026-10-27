// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/drivers/storage/spi_nand.h"
#include "dmm/os/os.h"
#include "dmm/services/storage.h"

namespace dmm::services {

/// The D878UVII's data flash: an SPI NAND with the radio's own bad-block
/// table.
///
/// Worn 128 KB blocks are replaced with spare ones, listed in the first
/// block: up to 125 records of 0x1F8 bytes at
/// 1 KB steps, the newest being the last whose byte 0x1F4 reads 0x55. A
/// record holds 125 little-endian block numbers (address / 128 KB) that are
/// replaced, then at 0xFA the 125 blocks replacing them, and at 0x1F6 the
/// number of entries in use.
class D878Uv2Storage final : public Storage
{
public:
    static constexpr std::uint32_t BlockBytes = 0x20000;
    static constexpr int MaxRemapEntries = 89;

    explicit D878Uv2Storage(drivers::storage::SpiNand &nand);

    /// Identifies the chip and loads the remap table. Call once.
    void start();

    StorageInfo info() override;
    StorageResult read(std::uint32_t address, std::span<std::uint8_t> data, bool physical = false) override;

    /// The block number @p block is read from (itself if not remapped).
    std::uint16_t remapBlock(std::uint16_t block) const;

    /// Erases the 128 KB block holding @p address (remapped) to 0xFF.
    StorageResult eraseBlock(std::uint32_t address);

    /// Programs @p data at @p address (remapped), within one 2 KB page.
    /// A page is normally programmed once, whole, after its erase; one
    /// programmed again reads back flagged by the chip's check.
    StorageResult program(std::uint32_t address, std::span<const std::uint8_t> data);

private:
    void loadRemapTable();
    StorageResult readPhysical(std::uint32_t address, std::span<std::uint8_t> data);

    std::uint32_t remapped(std::uint32_t address) const;
    bool writable();

    drivers::storage::SpiNand &m_nand;
    os::Mutex m_lock;
    bool m_present = false;
    bool m_unlocked = false;
    int m_entries = -1;
    std::uint16_t m_bad[125]{};
    std::uint16_t m_replacement[125]{};
};

} // namespace dmm::services
