// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/hal/gd32f30x/flash.h"

#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

struct Bank
{
    std::uintptr_t key;
    std::uintptr_t stat;
    std::uintptr_t ctl;
    std::uintptr_t addr;
};

Bank bankOf(std::uint32_t address)
{
    if (address < fmc::Bank1Start) {
        return {fmc::Key0, fmc::Stat0, fmc::Ctl0, fmc::Addr0};
    }
    return {fmc::Key1, fmc::Stat1, fmc::Ctl1, fmc::Addr1};
}

void unlock(const Bank &bank)
{
    if ((reg(bank.ctl) & fmc::Locked) != 0) {
        reg(bank.key) = fmc::UnlockKey1;
        reg(bank.key) = fmc::UnlockKey2;
    }
}

// Waits for the operation to end, clears its flags and tells whether it
// went well.
bool finish(const Bank &bank)
{
    while ((reg(bank.stat) & fmc::Busy) != 0) {
    }
    const std::uint32_t stat = reg(bank.stat);
    reg(bank.stat) = fmc::EndFlag | fmc::ProgramError | fmc::ProtectError;
    return (stat & (fmc::ProgramError | fmc::ProtectError)) == 0;
}

bool inFlash(std::uint32_t address)
{
    return address >= ProgramFlashBase && address < ProgramFlashEnd;
}

} // namespace

std::uint32_t flashPageSize(std::uint32_t address)
{
    return address < fmc::Bank1Start ? 2048 : 4096;
}

bool flashErasePage(std::uint32_t address)
{
    if (!inFlash(address) || address % flashPageSize(address) != 0) {
        return false;
    }
    const Bank bank = bankOf(address);
    unlock(bank);
    finish(bank);
    reg(bank.ctl) |= fmc::PageErase;
    reg(bank.addr) = address;
    reg(bank.ctl) |= fmc::Start;
    const bool ok = finish(bank);
    reg(bank.ctl) &= ~fmc::PageErase;
    reg(bank.ctl) |= fmc::Locked;
    return ok;
}

bool flashProgramWord(std::uint32_t address, std::uint32_t value)
{
    if (!inFlash(address) || address % 4 != 0) {
        return false;
    }
    const Bank bank = bankOf(address);
    unlock(bank);
    finish(bank);
    reg(bank.ctl) |= fmc::Program;
    reg(address) = value;
    const bool ok = finish(bank);
    reg(bank.ctl) &= ~fmc::Program;
    reg(bank.ctl) |= fmc::Locked;
    return ok && reg(address) == value;
}

} // namespace dmm::hal::gd32f30x
