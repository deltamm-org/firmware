// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// Crash capture. A fault, a failed kernel check or an unexpected interrupt
// writes a record to RAM that start-up code leaves alone, then restarts
// the MCU. The next boot reads the record once.

#include <cstdint>

namespace dmm::hal::cortex_m {

enum class CrashKind : std::uint8_t
{
    None,
    HardFault,
    UnexpectedInterrupt,
    StackOverflow,
    KernelCheck,
    TestFault,
    /// The watchdog went unfed: a task kept the CPU from the one that
    /// feeds it, or that task stopped.
    Stall,
};

struct CrashRecord
{
    CrashKind kind = CrashKind::None;
    /// For UnexpectedInterrupt: the exception number. For KernelCheck: the
    /// source line. For Stall: the milliseconds since the last feed.
    std::uint32_t detail = 0;
    /// Where it happened: the faulting instruction, or the caller.
    std::uint32_t pc = 0;
    std::uint32_t lr = 0;
    std::uint32_t psr = 0;
    /// Configurable and hard fault status, and the fault address when the
    /// status says it is valid.
    std::uint32_t cfsr = 0;
    std::uint32_t hfsr = 0;
    std::uint32_t faultAddress = 0;
    char task[12] = {};
};

/// Where records get the running task's name; returns nullptr outside one.
using TaskNameSource = const char *(*)();
void setTaskNameSource(TaskNameSource source);

/// Records a crash that has no exception frame, then restarts. @p detail as
/// in CrashRecord.
[[noreturn]] void crash(CrashKind kind, std::uint32_t detail, const char *task = nullptr);

/// From an interrupt that preempted a task: records a crash where that
/// task was interrupted (the pc, lr and psr it stacked on its process
/// stack), then restarts.
[[noreturn]] void crashInterruptedTask(CrashKind kind, std::uint32_t detail, const char *task);

/// The record left by the previous run, if any; clears it.
bool takeCrashRecord(CrashRecord &record);

/// Executes an undefined instruction, to test crash capture.
[[noreturn]] void triggerTestFault();

} // namespace dmm::hal::cortex_m
