// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/cortex_m/fault.h"

#include "dmm/hal/cortex_m/core.h"

#include <cstddef>
#include <type_traits>

namespace dmm::hal::cortex_m {

namespace {

constexpr std::uint32_t Magic = 0x44434D52; // "DCMR"

static_assert(std::is_trivially_copyable_v<CrashRecord>);

struct StoredRecord
{
    std::uint32_t magic;
    CrashRecord record;
    std::uint32_t check;
};

// In .noinit: neither copied nor cleared at start-up. Raw storage, so no
// constructor runs on it either.
[[gnu::section(".noinit")]] alignas(StoredRecord) std::uint8_t storedBytes[sizeof(StoredRecord)];

StoredRecord &stored()
{
    return *reinterpret_cast<StoredRecord *>(storedBytes);
}

TaskNameSource taskNameSource = nullptr;

namespace reg_address {
constexpr std::uintptr_t Cfsr = 0xE000ED28;
constexpr std::uintptr_t Hfsr = 0xE000ED2C;
constexpr std::uintptr_t Mmfar = 0xE000ED34;
constexpr std::uintptr_t Bfar = 0xE000ED38;
} // namespace reg_address

constexpr std::uint32_t MmfarValid = 1u << 7;
constexpr std::uint32_t BfarValid = 1u << 15;

std::uint32_t checksum(const StoredRecord &s)
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&s.record);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < sizeof s.record; ++i) {
        h = (h ^ bytes[i]) * 16777619u;
    }
    return h;
}

void copyName(char (&to)[12], const char *from)
{
    std::size_t i = 0;
    for (; from != nullptr && from[i] != '\0' && i + 1 < sizeof to; ++i) {
        to[i] = from[i];
    }
    for (; i < sizeof to; ++i) {
        to[i] = '\0';
    }
}

[[noreturn]] void store(CrashRecord record, const char *task)
{
    if (task == nullptr && taskNameSource != nullptr) {
        task = taskNameSource();
    }
    copyName(record.task, task);
    StoredRecord &s = stored();
    s.magic = Magic;
    s.record = record;
    s.check = checksum(s);
    systemReset();
}

bool insideRam(std::uint32_t address)
{
    return address >= 0x20000000u && address + 32 <= 0x20018000u;
}

} // namespace

void setTaskNameSource(TaskNameSource source)
{
    taskNameSource = source;
}

void crash(CrashKind kind, std::uint32_t detail, const char *task)
{
    disableInterrupts();
    CrashRecord r;
    r.kind = kind;
    r.detail = detail;
    r.lr = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    r.pc = r.lr;
    store(r, task);
}

void crashInterruptedTask(CrashKind kind, std::uint32_t detail, const char *task)
{
    disableInterrupts();
    CrashRecord r;
    r.kind = kind;
    r.detail = detail;
    std::uint32_t psp = 0;
    asm volatile("mrs %0, psp" : "=r"(psp));
    if (insideRam(psp)) {
        // The exception frame: r0-r3, r12, lr, pc, psr.
        const auto *frame = reinterpret_cast<const std::uint32_t *>(psp);
        r.lr = frame[5];
        r.pc = frame[6];
        r.psr = frame[7];
    }
    store(r, task);
}

bool takeCrashRecord(CrashRecord &record)
{
    StoredRecord &s = stored();
    const bool valid = s.magic == Magic && s.check == checksum(s);
    if (valid) {
        record = s.record;
    }
    s.magic = 0;
    return valid;
}

void triggerTestFault()
{
    asm volatile("udf #0");
    for (;;) {
    }
}

} // namespace dmm::hal::cortex_m

using namespace dmm::hal::cortex_m;

extern "C" {

// Called by the fault handlers with the stacked exception frame (r0-r3,
// r12, lr, pc, psr) and the exception number.
[[noreturn, gnu::used]] void dmmRecordFault(const std::uint32_t *frame, std::uint32_t exception)
{
    CrashRecord r;
    r.kind = exception == 3 ? CrashKind::HardFault : CrashKind::UnexpectedInterrupt;
    r.detail = exception;
    const std::uint32_t udfAt = reinterpret_cast<std::uintptr_t>(&triggerTestFault) & ~1u;
    if (insideRam(reinterpret_cast<std::uintptr_t>(frame))) {
        r.lr = frame[5];
        r.pc = frame[6];
        r.psr = frame[7];
    }
    r.cfsr = reg(reg_address::Cfsr);
    r.hfsr = reg(reg_address::Hfsr);
    if ((r.cfsr & BfarValid) != 0) {
        r.faultAddress = reg(reg_address::Bfar);
    } else if ((r.cfsr & MmfarValid) != 0) {
        r.faultAddress = reg(reg_address::Mmfar);
    }
    if (r.pc >= udfAt && r.pc < udfAt + 8) {
        r.kind = CrashKind::TestFault;
    }
    store(r, nullptr);
}

// Every fault and unexpected interrupt: pick the stack the frame is on,
// read the exception number, and record.
[[noreturn, gnu::naked]] void dmmFaultEntry()
{
    asm volatile("cpsid i\n\t"
                 "tst lr, #4\n\t"
                 "ite eq\n\t"
                 "mrseq r0, msp\n\t"
                 "mrsne r0, psp\n\t"
                 "mrs r1, ipsr\n\t"
                 "b dmmRecordFault\n\t");
}

} // extern "C"
