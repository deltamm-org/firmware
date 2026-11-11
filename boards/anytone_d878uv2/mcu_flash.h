// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The MCU's program flash as the loader logic's ProgramFlash, limited to
// one address range whatever the caller asks.

#include "dmm/hal/gd32f30x/flash.h"
#include "dmm/services/boot_flash.h"

#include <cstdint>
#include <cstring>
#include <span>

namespace board {

class McuFlash final : public dmm::services::boot::ProgramFlash
{
public:
    /// Erases and programs only inside @p writable.
    explicit constexpr McuFlash(dmm::services::boot::FlashArea writable)
        : m_writable(writable)
    {
    }

    bool read(std::uint32_t address, std::span<std::uint8_t> out) override
    {
        namespace hal = dmm::hal::gd32f30x;
        if (address < hal::ProgramFlashBase || address > hal::ProgramFlashEnd || out.size() > hal::ProgramFlashEnd - address) {
            return false;
        }
        std::memcpy(out.data(), reinterpret_cast<const void *>(address), out.size());
        return true;
    }

    bool eraseSector(std::uint32_t address) override
    {
        namespace hal = dmm::hal::gd32f30x;
        if (address % dmm::services::boot::SectorBytes != 0 || !m_writable.contains(address, dmm::services::boot::SectorBytes)) {
            return false;
        }
        for (std::uint32_t page = address; page < address + dmm::services::boot::SectorBytes; page += hal::flashPageSize(page)) {
            if (!hal::flashErasePage(page)) {
                return false;
            }
        }
        return true;
    }

    bool program(std::uint32_t address, std::span<const std::uint8_t> data) override
    {
        if (address % 4 != 0 || data.size() % 4 != 0 || !m_writable.contains(address, static_cast<std::uint32_t>(data.size()))) {
            return false;
        }
        for (std::size_t i = 0; i < data.size(); i += 4) {
            const std::uint32_t word = static_cast<std::uint32_t>(data[i]) | static_cast<std::uint32_t>(data[i + 1]) << 8 |
                static_cast<std::uint32_t>(data[i + 2]) << 16 | static_cast<std::uint32_t>(data[i + 3]) << 24;
            if (!dmm::hal::gd32f30x::flashProgramWord(address + static_cast<std::uint32_t>(i), word)) {
                return false;
            }
        }
        return true;
    }

private:
    dmm::services::boot::FlashArea m_writable;
};

} // namespace board
