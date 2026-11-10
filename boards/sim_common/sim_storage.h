// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_settings_store.h"
#include "dmm/services/storage.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <mutex>

/// A 256 MB data flash holding a calibration copy marked valid (no battery
/// offset) and a small sample codeplug: two zones, four channels and the
/// VFOs. Unwritten bytes read 0xFF, as on an erased chip.
class SimStorage final : public dmm::services::Storage
{
public:
    SimStorage();

    dmm::services::StorageInfo info() override;
    dmm::services::StorageResult read(std::uint32_t address, std::span<std::uint8_t> data, bool physical) override;
    void write(std::uint32_t address, std::span<const std::uint8_t> data);

private:
    void put(std::uint32_t address, std::uint8_t value) { m_bytes[address] = value; }
    void putBytes(std::uint32_t address, std::initializer_list<std::uint8_t> bytes);
    void markValid(std::uint32_t block);
    void putChannel(std::uint32_t address, std::uint32_t hz, std::int32_t shiftHz, bool wide, std::uint8_t ctcssIndex,
        std::uint8_t mode, std::uint8_t power, const char *name);
    void putSampleCodeplug();

    std::mutex m_lock;
    std::map<std::uint32_t, std::uint8_t> m_bytes;
};

/// Writes the settings store's pieces into the valid copies of their
/// blocks, for this run only.
dmm::services::SaveResult writeSettings(SimStorage &storage, dmm::services::D878Uv2Codeplug &codeplug,
    std::span<const dmm::services::D878Uv2SettingsStore::Piece> pieces);
