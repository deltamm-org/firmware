// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/spi_nand_bus.h"

#include <cstdint>
#include <map>
#include <vector>

// A Toshiba-style SPI NAND chip: understands read ID, page read, get and
// set feature, dual-output cache read, write enable, program load and
// execute and block erase, and records every command byte it sees.
// Unwritten bytes read 0xFF. Erase and program need write enable and
// unlocked blocks; programming only clears bits, and a page programmed
// more than once loads as uncorrectable, as on the real chip.
class FakeSpiNand final : public dmm::hal::SpiNandBus
{
public:
    std::uint8_t maker = 0x98;
    std::uint8_t device = 0xCB;
    /// Status read while busy, before the page is ready.
    int busyPolls = 1;
    /// Pages whose load reports these ECC status bits.
    std::map<std::uint32_t, std::uint8_t> eccStatus;
    /// Pages whose next loads report uncorrectable, then clean.
    std::map<std::uint32_t, int> flaggedLoads;

    std::map<std::uint32_t, std::uint8_t> memory;
    std::vector<std::uint8_t> opcodes;
    int pageLoads = 0;
    /// The block lock register; the chip comes up with every block locked.
    std::uint8_t blockLock = 0x38;
    /// Pages whose program, and blocks (first page) whose erase, fail.
    std::map<std::uint32_t, bool> failPrograms;
    std::map<std::uint32_t, bool> failErases;
    /// Times each page was programmed since its block's last erase.
    std::map<std::uint32_t, int> programs;
    int erases = 0;

    void put(std::uint32_t address, const std::vector<std::uint8_t> &bytes)
    {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            memory[address + static_cast<std::uint32_t>(i)] = bytes[i];
        }
    }

    void select() override
    {
        m_command.clear();
        m_idIndex = 0;
        m_selected = true;
    }
    void deselect() override
    {
        if (!m_command.empty() && m_command[0] == 0x13 && m_command.size() == 4) {
            m_page = static_cast<std::uint32_t>(m_command[1] << 16 | m_command[2] << 8 | m_command[3]);
            m_busyLeft = busyPolls;
            ++pageLoads;
            m_flagged = false;
            if (auto f = flaggedLoads.find(m_page); f != flaggedLoads.end() && f->second > 0) {
                --f->second;
                m_flagged = true;
            }
            if (auto p = programs.find(m_page); p != programs.end() && p->second > 1) {
                m_flagged = true;
            }
        } else if (m_command.size() == 1 && m_command[0] == 0x06) {
            m_writeEnabled = true;
        } else if (m_command.size() == 3 && m_command[0] == 0x1F && m_command[1] == 0xA0) {
            blockLock = m_command[2];
        } else if (m_command.size() >= 3 && m_command[0] == 0x02) {
            m_cache.assign(2048, 0xFF);
            const std::uint32_t column = static_cast<std::uint32_t>(m_command[1] << 8 | m_command[2]);
            for (std::size_t i = 3; i < m_command.size() && column + i - 3 < 2048; ++i) {
                m_cache[column + i - 3] = m_command[i];
            }
        } else if (m_command.size() == 4 && m_command[0] == 0x10) {
            const auto page = static_cast<std::uint32_t>(m_command[1] << 16 | m_command[2] << 8 | m_command[3]);
            m_failed = !m_writeEnabled || blockLock != 0 || failPrograms.count(page) != 0;
            if (!m_failed) {
                for (std::uint32_t i = 0; i < 2048; ++i) {
                    if (m_cache[i] != 0xFF) {
                        const auto it = memory.find(page * 2048 + i);
                        memory[page * 2048 + i] = static_cast<std::uint8_t>((it == memory.end() ? 0xFF : it->second) & m_cache[i]);
                    }
                }
                ++programs[page];
            }
            m_failBit = 0x08;
            m_writeEnabled = false;
            m_busyLeft = busyPolls;
        } else if (m_command.size() == 4 && m_command[0] == 0xD8) {
            const auto page = static_cast<std::uint32_t>(m_command[1] << 16 | m_command[2] << 8 | m_command[3]);
            const std::uint32_t first = page - page % 64;
            m_failed = !m_writeEnabled || blockLock != 0 || failErases.count(first) != 0;
            if (!m_failed) {
                memory.erase(memory.lower_bound(first * 2048), memory.lower_bound((first + 64) * 2048));
                for (std::uint32_t p = first; p < first + 64; ++p) {
                    programs.erase(p);
                }
                ++erases;
            }
            m_failBit = 0x04;
            m_writeEnabled = false;
            m_busyLeft = busyPolls;
        }
        m_selected = false;
    }
    void write(const std::uint8_t *bytes, std::size_t count) override
    {
        for (std::size_t i = 0; i < count; ++i) {
            if (m_command.empty()) {
                opcodes.push_back(bytes[i]);
            }
            m_command.push_back(bytes[i]);
        }
        if (m_command.size() == 4 && m_command[0] == 0x3B) {
            m_column = static_cast<std::uint32_t>(m_command[1] << 8 | m_command[2]);
        }
    }
    void read(std::uint8_t *bytes, std::size_t count) override
    {
        for (std::size_t i = 0; i < count; ++i) {
            bytes[i] = next(false);
        }
    }
    void readDual(std::uint8_t *bytes, std::size_t count) override
    {
        for (std::size_t i = 0; i < count; ++i) {
            bytes[i] = next(true);
        }
    }

private:
    std::uint8_t next(bool dual)
    {
        if (m_command.size() == 2 && m_command[0] == 0x9F) {
            return m_idIndex++ == 0 ? maker : device;
        }
        if (m_command.size() == 2 && m_command[0] == 0x0F && m_command[1] == 0xC0) {
            if (m_busyLeft > 0) {
                --m_busyLeft;
                return 0x01;
            }
            if (m_failed) {
                m_failed = false;
                return m_failBit;
            }
            if (m_flagged) {
                return 0x20;
            }
            const auto e = eccStatus.find(m_page);
            return e == eccStatus.end() ? 0x00 : e->second;
        }
        if (dual && !m_command.empty() && m_command[0] == 0x3B) {
            const std::uint32_t address = m_page * 2048 + m_column++;
            const auto it = memory.find(address);
            return it == memory.end() ? 0xFF : it->second;
        }
        return 0xEE;
    }

    std::vector<std::uint8_t> m_command;
    bool m_selected = false;
    std::uint32_t m_page = 0;
    std::uint32_t m_column = 0;
    int m_busyLeft = 0;
    bool m_flagged = false;
    int m_idIndex = 0;
    bool m_writeEnabled = false;
    bool m_failed = false;
    std::uint8_t m_failBit = 0;
    std::vector<std::uint8_t> m_cache = std::vector<std::uint8_t>(2048, 0xFF);
};
