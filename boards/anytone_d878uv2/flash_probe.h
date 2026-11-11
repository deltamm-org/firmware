// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// Flash speed probe (DMM_FLASH_PROBE builds): times identical code and
// data reads early in flash, past the first 256 KB and in the second bank.
// The console command "board flashprobe" runs it.

#include <cstdint>
#include <string_view>

namespace board {

/// Core clock cycles of each test; index 0 early in flash, 1 at
/// 0x08040000, 2 at 0x08080000.
struct FlashProbeResult
{
    /// 2000 passes of 64 straight-line instructions.
    std::uint32_t straight[3];
    /// 2000 passes of 32 taken branches.
    std::uint32_t jumps[3];
    /// 32 reads of a 4 KB table, by code early in flash.
    std::uint32_t reads[3];
};

/// Runs the tests with interrupts off (a few milliseconds). Needs the
/// cycle counter running.
FlashProbeResult runFlashProbe();

/// Runs the tests and returns one line: cycles per code pass, per jump
/// pass and per word read, at each of the three places.
std::string_view flashProbeReport();

} // namespace board
