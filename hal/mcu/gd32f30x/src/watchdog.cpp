// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/watchdog.h"

#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

constexpr std::uint32_t RcHz = 40'000;
constexpr std::uint32_t MaxReload = 0xFFF;
// Prescaler field n divides by 4 << n, up to 256.
constexpr std::uint32_t MaxPrescaler = 6;

} // namespace

void startWatchdog(std::uint32_t timeoutMs)
{
    // The finest prescaler whose reload value still fits.
    std::uint32_t prescaler = 0;
    std::uint32_t reload = timeoutMs * (RcHz / 4) / 1000;
    while (reload > MaxReload && prescaler < MaxPrescaler) {
        ++prescaler;
        reload = timeoutMs * (RcHz / (4u << prescaler)) / 1000;
    }
    if (reload > MaxReload) {
        reload = MaxReload;
    }
    reg(fwdgt::Ctl) = fwdgt::KeyUnlock;
    while ((reg(fwdgt::Stat) & fwdgt::PrescalerBusy) != 0) {
    }
    reg(fwdgt::Psc) = prescaler;
    while ((reg(fwdgt::Stat) & fwdgt::ReloadBusy) != 0) {
    }
    reg(fwdgt::Rld) = reload;
    reg(fwdgt::Ctl) = fwdgt::KeyReload;
    reg(fwdgt::Ctl) = fwdgt::KeyStart;
}

void feedWatchdog()
{
    reg(fwdgt::Ctl) = fwdgt::KeyReload;
}

} // namespace dmm::hal::gd32f30x
