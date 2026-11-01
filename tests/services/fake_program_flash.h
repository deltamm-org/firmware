// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/boot_flash.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

/// NOR program flash as the GD32F30x has it: pages of 2 KB below
/// @p bankBoundary and 4 KB above, erased to 0xFF one page at a time, and
/// 32-bit words programmed only when erased (or to 0). Counts its erase
/// and program operations, and can lose power during operation number k:
/// that operation is left half done and every later one fails.
class FakeProgramFlash final : public dmm::services::boot::ProgramFlash
{
public:
    FakeProgramFlash(std::uint32_t base, std::uint32_t size, std::uint32_t bankBoundary)
        : m_base(base), m_bankBoundary(bankBoundary), m_bytes(size, 0xFF)
    {
    }

    /// Loses power during operation @p k (counted from 0 since the last
    /// powerOn()); -1 = never.
    void cutAt(long k) { m_cutAt = k; }
    void powerOn()
    {
        m_powered = true;
        m_ops = 0;
        m_cutAt = -1;
    }
    bool powered() const { return m_powered; }
    long operations() const { return m_ops; }
    unsigned erases(std::uint32_t pageAddress) const
    {
        const auto it = m_eraseCount.find(pageAddress);
        return it == m_eraseCount.end() ? 0 : it->second;
    }
    std::uint32_t pageSize(std::uint32_t address) const { return address < m_bankBoundary ? 2048 : 4096; }

    std::uint8_t *at(std::uint32_t address) { return m_bytes.data() + (address - m_base); }
    const std::vector<std::uint8_t> &bytes() const { return m_bytes; }
    std::vector<std::uint8_t> &bytes() { return m_bytes; }

    bool read(std::uint32_t address, std::span<std::uint8_t> out) override
    {
        if (!inside(address, out.size())) {
            return false;
        }
        std::memcpy(out.data(), at(address), out.size());
        return true;
    }

    bool eraseSector(std::uint32_t address) override
    {
        if (address % dmm::services::boot::SectorBytes != 0 || !inside(address, dmm::services::boot::SectorBytes)) {
            return false;
        }
        for (std::uint32_t page = address; page < address + dmm::services::boot::SectorBytes; page += pageSize(page)) {
            if (!startOperation()) {
                return false;
            }
            if (m_ops - 1 == m_cutAt) {
                // Half erased: some bits up, the rest as they were.
                for (std::uint32_t i = 0; i < pageSize(page); ++i) {
                    *at(page + i) |= static_cast<std::uint8_t>(0x5A ^ (i & 0xFF));
                }
                m_powered = false;
                return false;
            }
            std::memset(at(page), 0xFF, pageSize(page));
            ++m_eraseCount[page];
        }
        return true;
    }

    bool program(std::uint32_t address, std::span<const std::uint8_t> data) override
    {
        if (address % 4 != 0 || data.size() % 4 != 0 || !inside(address, data.size()) || !startOperation()) {
            return false;
        }
        const bool cut = m_ops - 1 == m_cutAt;
        const std::size_t words = data.size() / 4;
        const std::size_t stopAt = cut ? words / 2 : words;
        for (std::size_t w = 0; w < words; ++w) {
            std::uint8_t *target = at(address + static_cast<std::uint32_t>(4 * w));
            const std::uint8_t *value = data.data() + 4 * w;
            const bool erased = target[0] == 0xFF && target[1] == 0xFF && target[2] == 0xFF && target[3] == 0xFF;
            const bool zero = value[0] == 0 && value[1] == 0 && value[2] == 0 && value[3] == 0;
            if (w == stopAt) {
                // Cut while this word was being programmed: only some of
                // its bits came down.
                for (int i = 0; i < 4; ++i) {
                    target[i] &= static_cast<std::uint8_t>(value[i] | 0x0F);
                }
                m_powered = false;
                return false;
            }
            if (!erased && !zero) {
                ++programErrors;
                return false;
            }
            for (int i = 0; i < 4; ++i) {
                target[i] = zero ? 0 : value[i];
            }
        }
        return true;
    }

    unsigned programErrors = 0;

private:
    bool inside(std::uint32_t address, std::size_t length) const
    {
        return address >= m_base && address - m_base <= m_bytes.size() && length <= m_bytes.size() - (address - m_base);
    }

    bool startOperation()
    {
        if (!m_powered) {
            return false;
        }
        ++m_ops;
        return true;
    }

    std::uint32_t m_base;
    std::uint32_t m_bankBoundary;
    std::vector<std::uint8_t> m_bytes;
    bool m_powered = true;
    long m_ops = 0;
    long m_cutAt = -1;
    std::map<std::uint32_t, unsigned> m_eraseCount;
};
