// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "flash_probe.h"

#include "dmm/hal/cortex_m/core.h"

#include "dmm/lib/format.h"

#include <array>
#include <cstdint>

// The same code and the same table three times: early in the image, at
// 0x08040000 (the first address past the 256 KB the GD32F30x manual names
// as executed without wait states) and at 0x08080000 (the first address of
// the second flash bank). The linker script places the sections.

namespace {

namespace cm = dmm::hal::cortex_m;

constexpr std::uint32_t TableWords = 1024;

constexpr std::array<std::uint32_t, TableWords> table()
{
    std::array<std::uint32_t, TableWords> t{};
    for (std::uint32_t i = 0; i < TableWords; ++i) {
        t[i] = i * 2654435761u;
    }
    return t;
}

} // namespace

// Straight-line code: per pass 32 16-bit and 32 32-bit instructions.
#define DMM_PROBE_STRAIGHT(name, section_name)                         \
    extern "C" __attribute__((naked, noinline, section(section_name))) \
    std::uint32_t                                                      \
    name(std::uint32_t)                                                \
    {                                                                  \
        asm("movs r1, #0\n\t"                                          \
            "1:\n\t"                                                   \
            ".rept 32\n\t"                                             \
            "adds r1, #1\n\t"                                          \
            "add.w r2, r2, #3\n\t"                                     \
            ".endr\n\t"                                                \
            "subs r0, #1\n\t"                                          \
            "bne 1b\n\t"                                               \
            "mov r0, r1\n\t"                                           \
            "bx lr");                                                  \
    }

// Taken branches: per pass 32 jumps over one instruction each.
#define DMM_PROBE_JUMPS(name, section_name)                            \
    extern "C" __attribute__((naked, noinline, section(section_name))) \
    std::uint32_t                                                      \
    name(std::uint32_t)                                                \
    {                                                                  \
        asm("1:\n\t"                                                   \
            ".rept 32\n\t"                                             \
            "b.n .+4\n\t"                                              \
            "nop\n\t"                                                  \
            ".endr\n\t"                                                \
            "subs r0, #1\n\t"                                          \
            "bne 1b\n\t"                                               \
            "bx lr");                                                  \
    }

DMM_PROBE_STRAIGHT(dmmProbeStraightNear, ".probe_near.text")
DMM_PROBE_STRAIGHT(dmmProbeStraightFar0, ".probe_far0.text")
DMM_PROBE_STRAIGHT(dmmProbeStraightFar1, ".probe_far1.text")
DMM_PROBE_JUMPS(dmmProbeJumpsNear, ".probe_near.text")
DMM_PROBE_JUMPS(dmmProbeJumpsFar0, ".probe_far0.text")
DMM_PROBE_JUMPS(dmmProbeJumpsFar1, ".probe_far1.text")

// Sums @p words words from @p table; runs from the early copy.
extern "C" __attribute__((naked, noinline, section(".probe_near.text"))) std::uint32_t dmmProbeRead(const std::uint32_t *, std::uint32_t)
{
    asm("movs r2, #0\n\t"
        "1:\n\t"
        "ldr r3, [r0], #4\n\t"
        "adds r2, r2, r3\n\t"
        "subs r1, #1\n\t"
        "bne 1b\n\t"
        "mov r0, r2\n\t"
        "bx lr");
}

__attribute__((section(".probe_near.rodata"), used)) extern const std::array<std::uint32_t, TableWords> dmmProbeTableNear = table();
__attribute__((section(".probe_far0.rodata"), used)) extern const std::array<std::uint32_t, TableWords> dmmProbeTableFar0 = table();
__attribute__((section(".probe_far1.rodata"), used)) extern const std::array<std::uint32_t, TableWords> dmmProbeTableFar1 = table();

namespace board {

namespace {

constexpr std::uint32_t CodePasses = 2000;
constexpr std::uint32_t TableReads = 32;

template <typename F>
std::uint32_t cycles(F f)
{
    const cm::InterruptLock lock;
    const std::uint32_t start = cm::reg(cm::dwt::Cyccnt);
    f();
    return cm::reg(cm::dwt::Cyccnt) - start;
}

std::uint32_t readCycles(const std::array<std::uint32_t, TableWords> &t)
{
    return cycles([&] {
        for (std::uint32_t i = 0; i < TableReads; ++i) {
            dmmProbeRead(t.data(), TableWords);
        }
    });
}

// @p total / @p count with two decimals.
void perItem(dmm::lib::StaticString<160> &text, std::uint32_t total, std::uint32_t count)
{
    const std::uint32_t hundredths = static_cast<std::uint32_t>((static_cast<std::uint64_t>(total) * 100 + count / 2) / count);
    text << hundredths / 100 << '.' << static_cast<char>('0' + hundredths / 10 % 10) << static_cast<char>('0' + hundredths % 10);
}

void three(dmm::lib::StaticString<160> &text, const char *name, const std::uint32_t (&values)[3], std::uint32_t count)
{
    text << ' ' << name << ' ';
    for (int i = 0; i < 3; ++i) {
        if (i != 0) {
            text << '/';
        }
        perItem(text, values[i], count);
    }
}

} // namespace

FlashProbeResult runFlashProbe()
{
    FlashProbeResult r;
    r.straight[0] = cycles([] { dmmProbeStraightNear(CodePasses); });
    r.straight[1] = cycles([] { dmmProbeStraightFar0(CodePasses); });
    r.straight[2] = cycles([] { dmmProbeStraightFar1(CodePasses); });
    r.jumps[0] = cycles([] { dmmProbeJumpsNear(CodePasses); });
    r.jumps[1] = cycles([] { dmmProbeJumpsFar0(CodePasses); });
    r.jumps[2] = cycles([] { dmmProbeJumpsFar1(CodePasses); });
    r.reads[0] = readCycles(dmmProbeTableNear);
    r.reads[1] = readCycles(dmmProbeTableFar0);
    r.reads[2] = readCycles(dmmProbeTableFar1);
    return r;
}

std::string_view flashProbeReport()
{
    static dmm::lib::StaticString<160> text;
    const FlashProbeResult r = runFlashProbe();
    text.clear();
    text << "flash probe, cycles below 256K/past 256K/bank 1:";
    three(text, "code pass", r.straight, CodePasses);
    three(text, "jump pass", r.jumps, CodePasses);
    three(text, "word read", r.reads, TableReads * TableWords);
    return text.view();
}

} // namespace board
