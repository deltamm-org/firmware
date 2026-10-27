// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>
#include <span>

namespace dmm::services {

enum class StorageResult : std::uint8_t
{
    Ok,
    /// Read, with bit errors the chip corrected.
    Corrected,
    /// Read, but the chip's check failed: the data may be wrong. Pages
    /// written in several pieces read like this, with correct data.
    Uncorrectable,
    /// The chip did not answer in time.
    Timeout,
    /// No usable storage chip.
    NoChip,
    /// Past the end of the chip.
    OutOfRange,
    /// The chip reported that an erase or a program failed.
    Failed,
};

constexpr bool usable(StorageResult r)
{
    return r == StorageResult::Ok || r == StorageResult::Corrected;
}

/// The data came back, though possibly flagged by the chip's check. The
/// caller then needs its own way to tell good data from bad.
constexpr bool readable(StorageResult r)
{
    return usable(r) || r == StorageResult::Uncorrectable;
}

const char *resultName(StorageResult r);

struct StorageInfo
{
    bool present = false;
    std::uint8_t maker = 0;
    std::uint8_t device = 0;
    std::uint32_t sizeBytes = 0;
    /// Blocks moved elsewhere; -1 = no remap table found.
    int remappedBlocks = -1;
};

/// The radio's data flash, read only. Logical reads follow the bad-block
/// remapping, physical reads do not.
/// Safe to use from several tasks.
class Storage
{
public:
    virtual StorageInfo info() = 0;
    virtual StorageResult read(std::uint32_t address, std::span<std::uint8_t> data, bool physical = false) = 0;

protected:
    ~Storage() = default;
};

} // namespace dmm::services
