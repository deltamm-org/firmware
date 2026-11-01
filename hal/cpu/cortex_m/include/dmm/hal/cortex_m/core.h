// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// Cortex-M3/M4 core registers (Arm v7-M Architecture Reference Manual) and
// the few core operations every board needs.

#include <cstdint>

namespace dmm::hal::cortex_m {

inline volatile std::uint32_t &reg(std::uintptr_t address)
{
    return *reinterpret_cast<volatile std::uint32_t *>(address);
}

namespace scb {
inline constexpr std::uintptr_t Vtor = 0xE000ED08;
inline constexpr std::uintptr_t Aircr = 0xE000ED0C;
inline constexpr std::uintptr_t Cpacr = 0xE000ED88;
inline constexpr std::uint32_t AircrKey = 0x05FAu << 16;
inline constexpr std::uint32_t AircrSysResetReq = 1u << 2;
} // namespace scb

namespace nvic {
inline constexpr std::uintptr_t Iser = 0xE000E100;
inline constexpr std::uintptr_t Icer = 0xE000E180;
inline constexpr std::uintptr_t Icpr = 0xE000E280;
inline constexpr std::uintptr_t Ipr = 0xE000E400;
} // namespace nvic

namespace systick {
inline constexpr std::uintptr_t Ctrl = 0xE000E010;
} // namespace systick

namespace dwt {
inline constexpr std::uintptr_t Demcr = 0xE000EDFC;
inline constexpr std::uintptr_t Ctrl = 0xE0001000;
inline constexpr std::uintptr_t Cyccnt = 0xE0001004;
} // namespace dwt

/// Starts the core's cycle counter, used by busyWaitCycles().
inline void enableCycleCounter()
{
    reg(dwt::Demcr) |= 1u << 24; // trace enable
    reg(dwt::Cyccnt) = 0;
    reg(dwt::Ctrl) |= 1u;
}

/// Spins for at least @p cycles core clock cycles. For short hardware
/// settling times only; tasks wait with the OS.
inline void busyWaitCycles(std::uint32_t cycles)
{
    const std::uint32_t start = reg(dwt::Cyccnt);
    while (reg(dwt::Cyccnt) - start < cycles) {
    }
}

inline void disableInterrupts()
{
    asm volatile("cpsid i" ::: "memory");
}

inline void enableInterrupts()
{
    asm volatile("cpsie i" ::: "memory");
}

/// Masks interrupts while it lives, then puts the previous mask back, so it
/// nests. For a few instructions only.
class InterruptLock
{
public:
    InterruptLock() { asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(m_primask)::"memory"); }
    ~InterruptLock() { asm volatile("msr primask, %0" ::"r"(m_primask) : "memory"); }
    InterruptLock(const InterruptLock &) = delete;
    InterruptLock &operator=(const InterruptLock &) = delete;

private:
    std::uint32_t m_primask;
};

inline void dataSyncBarrier()
{
    asm volatile("dsb" ::: "memory");
}

inline void instructionSyncBarrier()
{
    asm volatile("isb" ::: "memory");
}

/// Sleeps until the next interrupt.
inline void waitForInterrupt()
{
    asm volatile("dsb\n\twfi\n\tisb" ::: "memory");
}

/// Grants full access to the floating-point unit (CP10 and CP11).
inline void enableFpu()
{
    reg(scb::Cpacr) |= 0xFu << 20;
    dataSyncBarrier();
    instructionSyncBarrier();
}

/// All implemented priority bits pre-empt; none are sub-priority.
inline void usePreemptOnlyPriorities()
{
    reg(scb::Aircr) = scb::AircrKey | (3u << 8);
}

/// Masks and clears every external interrupt and stops the SysTick timer,
/// so nothing left running by a boot loader fires into this image.
inline void resetInterruptState(unsigned irqCount)
{
    reg(systick::Ctrl) = 0;
    for (unsigned i = 0; i < (irqCount + 31) / 32; ++i) {
        reg(nvic::Icer + 4 * i) = 0xFFFFFFFFu;
        reg(nvic::Icpr + 4 * i) = 0xFFFFFFFFu;
    }
    dataSyncBarrier();
    instructionSyncBarrier();
}

inline void setVectorTable(const void *table)
{
    reg(scb::Vtor) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table));
    dataSyncBarrier();
}

/// Sets an external interrupt's priority; 0 is the most urgent. Only the
/// top @p priorityBits bits of each priority byte exist.
inline void setIrqPriority(unsigned irq, unsigned priority, unsigned priorityBits)
{
    *reinterpret_cast<volatile std::uint8_t *>(nvic::Ipr + irq) = static_cast<std::uint8_t>(priority << (8 - priorityBits));
}

inline void enableIrq(unsigned irq)
{
    reg(nvic::Iser + 4 * (irq / 32)) = 1u << (irq % 32);
}

inline void disableIrq(unsigned irq)
{
    reg(nvic::Icer + 4 * (irq / 32)) = 1u << (irq % 32);
}

/// Starts the image whose vector table is at @p base: points the vector
/// table there, loads its stack pointer and jumps to its reset handler.
/// Interrupts should be masked.
[[noreturn]] inline void startImageAt(std::uint32_t base)
{
    const auto *table = reinterpret_cast<const volatile std::uint32_t *>(base);
    reg(scb::Vtor) = base;
    dataSyncBarrier();
    instructionSyncBarrier();
    asm volatile("msr msp, %0\n\t"
                 "bx %1" ::"r"(table[0]),
        "r"(table[1]) : "memory");
    __builtin_unreachable();
}

/// Restarts the whole MCU.
[[noreturn]] inline void systemReset()
{
    dataSyncBarrier();
    reg(scb::Aircr) = scb::AircrKey | (reg(scb::Aircr) & (7u << 8)) | scb::AircrSysResetReq;
    dataSyncBarrier();
    for (;;) {
    }
}

} // namespace dmm::hal::cortex_m
