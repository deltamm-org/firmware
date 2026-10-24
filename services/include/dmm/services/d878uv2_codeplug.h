// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/codeplug.h"
#include "dmm/services/storage.h"

#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services {

/// Decodes a 64-byte D878UVII channel record; nothing when it is not a channel.
std::optional<Channel> decodeChannel(std::span<const std::uint8_t, 64> record);

/// Decodes the start of the D878UVII general settings block (at least 0xC0 bytes).
CodeplugSettings decodeSettings(std::span<const std::uint8_t> block);

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

private:
    bool copyValid(std::uint32_t block, bool &valid);
    bool bitSet(std::uint32_t bitmap, unsigned index);

    Storage &m_storage;
    // Copy choice per block, remembered: blocks and whether the first copy
    // is valid.
    static constexpr std::size_t Cached = 16;
    std::uint32_t m_cachedBlock[Cached] = {};
    bool m_cachedValid[Cached] = {};
    std::size_t m_cachedCount = 0;
};

} // namespace dmm::services
