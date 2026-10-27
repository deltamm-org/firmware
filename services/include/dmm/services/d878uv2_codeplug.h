// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/codeplug.h"
#include "dmm/services/d878uv2_storage.h"
#include "dmm/services/storage.h"

#include <array>

#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services {

/// Decodes a 64-byte D878UVII channel record; nothing when it is not a
/// channel. The DMR contact and own ID are left for the codeplug to look up
/// (channelLinks()).
std::optional<Channel> decodeChannel(std::span<const std::uint8_t, 64> record);

/// The lists a D878UVII channel record points into.
struct ChannelLinks
{
    std::uint32_t contact; // index into the contact list
    std::uint8_t radioId;  // index into the radio ID list
};
ChannelLinks channelLinks(std::span<const std::uint8_t, 64> record);

/// Decodes the start of the D878UVII general settings block (at least 0xC0 bytes).
CodeplugSettings decodeSettings(std::span<const std::uint8_t> block);
/// Adds what the extended general settings block holds (at least 0x40
/// bytes): the GPS satellite systems.
void decodeSettingsExtension(std::span<const std::uint8_t> block, CodeplugSettings &settings);

/// The D878UVII's codeplug on its data flash, read only. Every 128 KB
/// block is kept twice, in the block itself and the one after it; reads
/// come from the copy marked valid.
class D878Uv2Codeplug final : public CodeplugSource
{
public:
    explicit D878Uv2Codeplug(Storage &storage)
        : m_storage(storage)
    {
    }

    /// Reads @p data from codeplug address @p address, from the valid copy
    /// of each 128 KB block it touches.
    StorageResult read(std::uint32_t address, std::span<std::uint8_t> data);

    std::optional<CodeplugSettings> settings() override;
    std::optional<Channel> channel(std::uint16_t index) override;
    std::optional<Channel> vfo(bool sideB) override;
    bool zoneInUse(std::uint8_t zone) override;
    std::size_t zonesInUse(std::span<std::uint8_t> out) override;
    bool zoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name) override;
    std::size_t zoneChannels(std::uint8_t zone, std::span<std::uint16_t> out) override;
    std::uint16_t zoneSelection(std::uint8_t zone, bool sideB) override;
    bool contactName(std::uint32_t id, bool group, std::span<char, ContactNameBytes + 1> name) override;
    std::size_t receiveGroups(std::uint8_t list, std::span<std::uint32_t> out) override;
    bool dmrUser(std::uint32_t id, DmrUser &user) override;

    /// Contact @p index, if it is in use.
    std::optional<dmr::Target> contact(std::uint32_t index);
    /// The DMR ID in radio ID list entry @p index, if it is in use.
    std::optional<std::uint32_t> radioId(std::uint8_t index);

private:
    friend class D878Uv2CodeplugWriter;

    std::optional<Channel> readChannel(std::uint32_t address);
    bool copyValid(std::uint32_t block, bool &valid);
    // Drops the remembered copy choice of @p block, after a write.
    void forget(std::uint32_t block);
    bool bitSet(std::uint32_t bitmap, unsigned index);

    Storage &m_storage;
    // Copy choice per block, remembered: blocks and whether the first copy
    // is valid.
    static constexpr std::size_t Cached = 16;
    std::uint32_t m_cachedBlock[Cached] = {};
    bool m_cachedValid[Cached] = {};
    std::size_t m_cachedCount = 0;
};

/// Writes codeplug data as codeplug tools send it: in rising address
/// order, in pieces of any size. Each 128 KB block it touches gets a new
/// copy in the block's other half: erased, filled page by page from the
/// valid copy with the new data on top, its last page marked valid, then
/// the old copy marked superseded. Until then the old copy stays valid, so
/// an interrupted write leaves the codeplug as it was. Only the codeplug's
/// own blocks are written: never calibration, radio data or system areas.
class D878Uv2CodeplugWriter
{
public:
    /// The addresses codeplugs use: from the first channel bank to below
    /// the radio's own data at 0x02F00000, and the contact ID table and
    /// the callsign limits at 0x04800000.
    struct Range
    {
        std::uint32_t first;
        std::uint32_t end;
    };
    static constexpr Range Ranges[] = {{0x00800000, 0x02F00000}, {0x04800000, 0x04880000}};

    D878Uv2CodeplugWriter(D878Uv2Storage &storage, D878Uv2Codeplug &codeplug)
        : m_storage(storage), m_codeplug(codeplug)
    {
    }

    /// Takes @p data for @p address. False if refused (outside the
    /// codeplug, or below data already written) or the flash failed; after
    /// a failure every write is refused.
    bool write(std::uint32_t address, std::span<const std::uint8_t> data);

    /// Completes the block in progress; false on a failure.
    bool finish();

    /// Drops the block in progress, whose old copy stays valid; for a
    /// write that never finished. Writing can start again.
    void abandon()
    {
        m_open = false;
        m_failed = false;
        m_wrote = false;
    }

    /// Something was written since the last finish.
    bool wrote() const { return m_wrote; }

private:
    static constexpr std::uint32_t BlockBytes = D878Uv2Storage::BlockBytes;
    static constexpr std::uint32_t PageBytes = drivers::storage::SpiNand::PageBytes;
    static constexpr std::uint32_t Pages = BlockBytes / PageBytes;

    // The whole of a write is checked before the flash is touched; one
    // write stays within one block.
    bool allowed(std::uint32_t address, std::size_t size) const;
    bool startBlock(std::uint32_t block);
    bool loadPage(std::uint32_t page);
    bool programPage();
    bool finishBlock();

    D878Uv2Storage &m_storage;
    D878Uv2Codeplug &m_codeplug;
    bool m_open = false;
    bool m_failed = false;
    bool m_wrote = false;
    std::uint32_t m_block = 0;  // the block as codeplugs address it
    std::uint32_t m_source = 0; // its valid copy
    std::uint32_t m_target = 0; // the copy being written
    std::uint32_t m_page = 0;   // in the buffer
    std::array<std::uint8_t, PageBytes> m_buffer{};
};

} // namespace dmm::services
