// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services::boot {

/// The unit the loader erases and copies in.
inline constexpr std::uint32_t SectorBytes = 4096;

/// The MCU's program flash as the loader and the app see it. Erased flash
/// reads 0xFF. A program writes whole 32-bit words, each into an erased
/// word, or 0 over any word.
class ProgramFlash
{
public:
    /// Copies @p out.size() bytes from @p address.
    virtual bool read(std::uint32_t address, std::span<std::uint8_t> out) = 0;
    /// Erases the sector at @p address (a multiple of SectorBytes).
    virtual bool eraseSector(std::uint32_t address) = 0;
    /// Programs @p data at @p address; both a multiple of 4 bytes long or
    /// apart. Stops at the first word that fails.
    virtual bool program(std::uint32_t address, std::span<const std::uint8_t> data) = 0;

protected:
    ~ProgramFlash() = default;
};

/// An address range [begin, end).
struct FlashArea
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    bool contains(std::uint32_t address, std::uint32_t length) const
    {
        return address >= begin && address <= end && length <= end - address;
    }
};

/// Passes erases and programs on only inside the given areas, and reads
/// anywhere. Everything that writes flash goes through one of these.
class GuardedFlash final : public ProgramFlash
{
public:
    GuardedFlash(ProgramFlash &flash, std::span<const FlashArea> writable)
        : m_flash(flash), m_writable(writable)
    {
    }

    bool read(std::uint32_t address, std::span<std::uint8_t> out) override { return m_flash.read(address, out); }

    bool eraseSector(std::uint32_t address) override
    {
        if (address % SectorBytes != 0 || !allowed(address, SectorBytes)) {
            ++m_refused;
            return false;
        }
        return m_flash.eraseSector(address);
    }

    bool program(std::uint32_t address, std::span<const std::uint8_t> data) override
    {
        if (address % 4 != 0 || data.size() % 4 != 0 || !allowed(address, static_cast<std::uint32_t>(data.size()))) {
            ++m_refused;
            return false;
        }
        return m_flash.program(address, data);
    }

    /// How many requests were refused.
    unsigned refused() const { return m_refused; }

private:
    bool allowed(std::uint32_t address, std::uint32_t length) const
    {
        for (const FlashArea &area : m_writable) {
            if (area.contains(address, length)) {
                return true;
            }
        }
        return false;
    }

    ProgramFlash &m_flash;
    std::span<const FlashArea> m_writable;
    unsigned m_refused = 0;
};

} // namespace dmm::services::boot
