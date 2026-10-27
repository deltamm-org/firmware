// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/spi_nand_bus.h"
#include "dmm/lib/fn_ref.h"

#include <cstddef>
#include <cstdint>

namespace dmm::drivers::storage {

/// A serial NAND flash with 2 KB pages, 64 pages to an erase block and
/// on-chip ECC. Commands from the common SPI NAND command set (Toshiba
/// serial NAND datasheets).
class SpiNand
{
public:
    static constexpr std::uint32_t PageBytes = 2048;
    static constexpr std::uint32_t PagesPerBlock = 64;
    static constexpr unsigned LoadTries = 4;

    struct Id
    {
        std::uint8_t maker = 0;
        std::uint8_t device = 0;
    };

    enum class Result : std::uint8_t
    {
        Ok,
        /// Bit errors were found and corrected by the chip.
        Corrected,
        /// The chip's check failed on every load: the data may be wrong.
        /// Pages written in several pieces always read like this, with
        /// correct data, so the caller decides whether to trust them.
        Uncorrectable,
        /// The chip did not finish loading the page.
        Timeout,
        /// The chip reported that an erase or a program failed: a worn
        /// block.
        Failed,
    };

    /// @p delayUs waits at least the given number of microseconds.
    SpiNand(hal::SpiNandBus &bus, lib::FnRef<void(unsigned)> delayUs);

    Id readId();

    /// True for the parts this driver knows; sets the page count.
    bool identify();
    bool identified() const { return m_pageCount != 0; }
    Id id() const { return m_id; }
    std::uint32_t pageCount() const { return m_pageCount; }

    /// Reads @p count bytes of page @p page from byte @p column on (within
    /// the 2 KB page). A page stays loaded in the chip, so further reads
    /// of the same page skip the load. A page the chip flags as
    /// uncorrectable is loaded up to LoadTries times.
    Result read(std::uint32_t page, std::uint16_t column, std::uint8_t *data, std::size_t count);

    /// Lifts the chip's block protection, which it may come up with; call
    /// before erasing or programming.
    void unlock();

    /// Erases the 64-page block holding @p page to all 0xFF.
    Result eraseBlock(std::uint32_t page);

    /// Programs @p count bytes into page @p page from byte @p column on.
    /// Programming only clears bits, so the page is normally erased first;
    /// a page programmed more than once reads back flagged by its ECC.
    Result program(std::uint32_t page, std::uint16_t column, const std::uint8_t *data, std::size_t count);

private:
    Result loadPage(std::uint32_t page);
    void writeEnable();
    // Waits for an operation to finish; the status, or nothing on timeout.
    bool waitReady(unsigned maxPolls, std::uint8_t &status);

    hal::SpiNandBus &m_bus;
    lib::FnRef<void(unsigned)> m_delayUs;
    Id m_id;
    std::uint32_t m_pageCount = 0;
    std::uint32_t m_loadedPage = 0xFFFFFFFFu;
    Result m_loadedResult = Result::Ok;
};

} // namespace dmm::drivers::storage
