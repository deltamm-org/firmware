// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/step_timer.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

// TIMER6 runs from the APB1 timer clock, the system clock; prescaled to
// 1 MHz the counter counts microseconds.
constexpr std::uint32_t Prescaler = SystemClockHz / 1'000'000 - 1;

volatile std::uint32_t &timer6(std::uintptr_t offset)
{
    return reg(timer::Timer6 + offset);
}

} // namespace

void StepTimer::configure(unsigned irqPriority)
{
    reg(rcu::Apb1En) |= rcu::Timer6Clock;
    [[maybe_unused]] const std::uint32_t enabled = reg(rcu::Apb1En);
    timer6(timer::Ctl0) = 0;
    timer6(timer::Psc) = Prescaler;
    timer6(timer::Car) = 0xFFFF;
    timer6(timer::Swevg) = timer::UpdateEvent; // loads the prescaler
    timer6(timer::Intf) = ~timer::UpdateInterrupt;
    timer6(timer::Dmainten) = timer::UpdateInterrupt;
    cortex_m::setIrqPriority(irq::Timer6, irqPriority, irq::PriorityBits);
    cortex_m::enableIrq(irq::Timer6);
}

void StepTimer::start(std::uint32_t periodUs)
{
    timer6(timer::Ctl0) = 0;
    timer6(timer::Car) = (periodUs < 1 ? 1 : periodUs > 0xFFFF ? 0xFFFF
                                                               : periodUs) -
        1;
    timer6(timer::Cnt) = 0;
    timer6(timer::Intf) = ~timer::UpdateInterrupt;
    timer6(timer::Ctl0) = timer::CounterEnable;
}

void StepTimer::stop()
{
    timer6(timer::Ctl0) = 0;
    timer6(timer::Intf) = ~timer::UpdateInterrupt;
}

bool StepTimer::takeInterrupt()
{
    const bool update = (timer6(timer::Intf) & timer::UpdateInterrupt) != 0;
    timer6(timer::Intf) = ~timer::UpdateInterrupt;
    return update;
}

} // namespace dmm::hal::gd32f30x
