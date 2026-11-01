// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The program flash controller (FMC) of the 1 MB GD32F30x parts: erasing
// pages and programming 32-bit words, in both flash banks. Option bytes
// are not handled. The CPU stalls while it fetches from a bank that is
// being erased or programmed.

#include <cstdint>

namespace dmm::hal::gd32f30x {

inline constexpr std::uint32_t ProgramFlashBase = 0x08000000;
inline constexpr std::uint32_t ProgramFlashEnd = 0x08100000;

/// 2 KB in the first 512 KB (bank 0), 4 KB above (bank 1).
std::uint32_t flashPageSize(std::uint32_t address);

/// Erases the page at @p address (page-aligned). Takes up to about 300 ms.
bool flashErasePage(std::uint32_t address);

/// Programs @p value at @p address (word-aligned). The word must read
/// erased, unless @p value is 0. Takes up to about 90 us.
bool flashProgramWord(std::uint32_t address, std::uint32_t value);

} // namespace dmm::hal::gd32f30x
