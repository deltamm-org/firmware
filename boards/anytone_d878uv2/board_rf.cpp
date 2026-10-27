// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_rf.h"

#include "pins.h"

#include <atomic>

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/dac.h"
#include "dmm/hal/gd32f30x/exti.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/pwm.h"

namespace board {

namespace hal = dmm::hal::gd32f30x;

namespace cm = dmm::hal::cortex_m;

namespace {

hal::Timer2Pwm tunePwm{ReceiveTuneChannel};

// Digital transmit keying: the output control for a slot, 0 when not
// keying. Written by the radio task, read by the slot interrupt.
std::atomic<std::uint16_t> slotPower{0};

// Transmit path and amplifier on, then the output; off in the reverse
// order.
void keySlot(bool on, std::uint16_t power)
{
    if (on) {
        hal::write(TransmitEnable, true);
        hal::write(PowerAmplifierEnable, true);
        hal::dac::write(PowerControlChannel, power);
    } else {
        hal::dac::write(PowerControlChannel, 0);
        hal::write(PowerAmplifierEnable, false);
        hal::write(TransmitEnable, false);
    }
}

} // namespace

void BoardRf::configure()
{
    hal::write(TransmitEnable, false);
    hal::write(PowerAmplifierEnable, false);
    hal::configure(PowerAmplifierEnable, hal::PinMode::Output);
    hal::configure(PowerControl, hal::PinMode::Analog);
    hal::dac::start(PowerControlChannel, 0);
    hal::configure(ReceiveTune, hal::PinMode::Alternate, hal::PinSpeed::Medium);
    tunePwm.start(0, ReceiveTunePeriod);
    hal::configure(TransmitSlot, hal::PinMode::InputPullDown);
    hal::enableExti(TransmitSlot, hal::Edge::Both);
    cm::setIrqPriority(hal::irq::Exti0, TransmitSlotIrqPriority, hal::irq::PriorityBits);
    cm::enableIrq(hal::irq::Exti0);
}

void BoardRf::setSlotKeying(std::uint16_t value)
{
    if (value == 0) {
        slotPower.store(0, std::memory_order_relaxed);
        const cm::InterruptLock lock;
        keySlot(false, 0);
        return;
    }
    slotPower.store(value, std::memory_order_relaxed);
}

void BoardRf::onSlotEdge()
{
    if (!hal::takeExti(TransmitSlot.number)) {
        return;
    }
    const std::uint16_t power = slotPower.load(std::memory_order_relaxed);
    keySlot(power != 0 && hal::read(TransmitSlot), power);
}

void BoardRf::setReferenceTrim(std::uint16_t value)
{
    if (!m_trimStarted) {
        hal::configure(ReferenceTrim, hal::PinMode::Analog);
        hal::dac::start(ReferenceTrimChannel, value);
        m_trimStarted = true;
        return;
    }
    hal::dac::write(ReferenceTrimChannel, value);
}

void BoardRf::setReceiveTune(std::uint16_t value)
{
    tunePwm.setDuty(value > ReceiveTunePeriod ? ReceiveTunePeriod + 1 : value);
}

void BoardRf::setTransmitPath(bool on)
{
    hal::write(TransmitEnable, on);
}

void BoardRf::setPowerAmplifier(bool on)
{
    hal::write(PowerAmplifierEnable, on);
}

void BoardRf::setPowerControl(std::uint16_t value)
{
    hal::dac::write(PowerControlChannel, value);
}

} // namespace board
