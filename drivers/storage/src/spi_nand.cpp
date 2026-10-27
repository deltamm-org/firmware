// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/storage/spi_nand.h"

namespace dmm::drivers::storage {

namespace {

namespace cmd {
constexpr std::uint8_t ReadId = 0x9F;
constexpr std::uint8_t PageRead = 0x13;      // array to cache
constexpr std::uint8_t ReadCacheDual = 0x3B; // cache to host, 2 bits per clock
constexpr std::uint8_t GetFeature = 0x0F;
constexpr std::uint8_t SetFeature = 0x1F;
constexpr std::uint8_t WriteEnable = 0x06;
constexpr std::uint8_t ProgramLoad = 0x02;    // host to cache, rest of the cache 0xFF
constexpr std::uint8_t ProgramExecute = 0x10; // cache to array
constexpr std::uint8_t BlockErase = 0xD8;
} // namespace cmd

constexpr std::uint8_t BlockLockRegister = 0xA0;
constexpr std::uint8_t StatusRegister = 0xC0;
constexpr std::uint8_t Busy = 0x01;
constexpr std::uint8_t EraseFailed = 0x04;
constexpr std::uint8_t ProgramFailed = 0x08;
constexpr std::uint8_t EccMask = 0x30;
constexpr std::uint8_t EccUncorrectable = 0x20;

constexpr unsigned PollIntervalUs = 10;
constexpr unsigned MaxPolls = 1000; // 10 ms; a page load takes about 0.1 ms
// A program takes well under 1 ms, an erase a few ms.
constexpr unsigned MaxProgramPolls = 1000;
constexpr unsigned MaxErasePolls = 3000;

constexpr std::uint8_t MakerToshiba = 0x98;

// Toshiba 2 Gbit parts: 131072 pages.
constexpr std::uint32_t pagesFor(SpiNand::Id id)
{
    if (id.maker == MakerToshiba && (id.device == 0xCB || id.device == 0xEB)) {
        return 131072;
    }
    return 0;
}

} // namespace

SpiNand::SpiNand(hal::SpiNandBus &bus, lib::FnRef<void(unsigned)> delayUs)
    : m_bus(bus), m_delayUs(delayUs)
{
}

SpiNand::Id SpiNand::readId()
{
    const std::uint8_t request[] = {cmd::ReadId, 0x00};
    std::uint8_t reply[2] = {};
    m_bus.select();
    m_bus.write(request, sizeof request);
    m_bus.read(reply, sizeof reply);
    m_bus.deselect();
    return {reply[0], reply[1]};
}

bool SpiNand::identify()
{
    m_id = readId();
    m_pageCount = pagesFor(m_id);
    m_loadedPage = 0xFFFFFFFFu;
    return m_pageCount != 0;
}

SpiNand::Result SpiNand::loadPage(std::uint32_t page)
{
    const std::uint8_t request[] = {cmd::PageRead, static_cast<std::uint8_t>(page >> 16), static_cast<std::uint8_t>(page >> 8),
        static_cast<std::uint8_t>(page)};
    m_bus.select();
    m_bus.write(request, sizeof request);
    m_bus.deselect();

    std::uint8_t status = 0;
    if (!waitReady(MaxPolls, status)) {
        return Result::Timeout;
    }
    const std::uint8_t ecc = status & EccMask;
    return ecc == 0 ? Result::Ok : ecc == EccUncorrectable ? Result::Uncorrectable
                                                           : Result::Corrected;
}

bool SpiNand::waitReady(unsigned maxPolls, std::uint8_t &status)
{
    const std::uint8_t statusRequest[] = {cmd::GetFeature, StatusRegister};
    for (unsigned i = 0; i < maxPolls; ++i) {
        m_delayUs(PollIntervalUs);
        m_bus.select();
        m_bus.write(statusRequest, sizeof statusRequest);
        m_bus.read(&status, 1);
        m_bus.deselect();
        if ((status & Busy) == 0) {
            return true;
        }
    }
    return false;
}

void SpiNand::writeEnable()
{
    m_bus.select();
    m_bus.write(&cmd::WriteEnable, 1);
    m_bus.deselect();
}

void SpiNand::unlock()
{
    const std::uint8_t request[] = {cmd::SetFeature, BlockLockRegister, 0x00};
    m_bus.select();
    m_bus.write(request, sizeof request);
    m_bus.deselect();
}

SpiNand::Result SpiNand::eraseBlock(std::uint32_t page)
{
    page -= page % PagesPerBlock;
    m_loadedPage = 0xFFFFFFFFu;
    writeEnable();
    const std::uint8_t request[] = {cmd::BlockErase, static_cast<std::uint8_t>(page >> 16), static_cast<std::uint8_t>(page >> 8),
        static_cast<std::uint8_t>(page)};
    m_bus.select();
    m_bus.write(request, sizeof request);
    m_bus.deselect();
    std::uint8_t status = 0;
    if (!waitReady(MaxErasePolls, status)) {
        return Result::Timeout;
    }
    return (status & EraseFailed) != 0 ? Result::Failed : Result::Ok;
}

SpiNand::Result SpiNand::program(std::uint32_t page, std::uint16_t column, const std::uint8_t *data, std::size_t count)
{
    // The program goes through the chip's page cache, which held the page
    // loaded last.
    m_loadedPage = 0xFFFFFFFFu;
    writeEnable();
    const std::uint8_t load[] = {cmd::ProgramLoad, static_cast<std::uint8_t>(column >> 8), static_cast<std::uint8_t>(column)};
    m_bus.select();
    m_bus.write(load, sizeof load);
    m_bus.write(data, count);
    m_bus.deselect();
    const std::uint8_t execute[] = {cmd::ProgramExecute, static_cast<std::uint8_t>(page >> 16), static_cast<std::uint8_t>(page >> 8),
        static_cast<std::uint8_t>(page)};
    m_bus.select();
    m_bus.write(execute, sizeof execute);
    m_bus.deselect();
    std::uint8_t status = 0;
    if (!waitReady(MaxProgramPolls, status)) {
        return Result::Timeout;
    }
    return (status & ProgramFailed) != 0 ? Result::Failed : Result::Ok;
}

SpiNand::Result SpiNand::read(std::uint32_t page, std::uint16_t column, std::uint8_t *data, std::size_t count)
{
    if (page != m_loadedPage) {
        m_loadedResult = loadPage(page);
        for (unsigned i = 1; i < LoadTries && m_loadedResult == Result::Uncorrectable; ++i) {
            m_loadedResult = loadPage(page);
        }
        m_loadedPage = m_loadedResult == Result::Timeout ? 0xFFFFFFFFu : page;
        if (m_loadedResult == Result::Timeout) {
            return Result::Timeout;
        }
    }
    const std::uint8_t request[] = {cmd::ReadCacheDual, static_cast<std::uint8_t>(column >> 8),
        static_cast<std::uint8_t>(column), 0x00};
    m_bus.select();
    m_bus.write(request, sizeof request);
    m_bus.readDual(data, count);
    m_bus.deselect();
    return m_loadedResult;
}

} // namespace dmm::drivers::storage
