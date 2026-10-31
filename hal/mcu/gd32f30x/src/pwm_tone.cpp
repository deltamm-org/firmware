// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/pwm_tone.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

constexpr unsigned ToneChannel = 3;
constexpr std::uint32_t PwmPeriod = 255;
constexpr std::uint32_t SamplePeriod = SystemClockHz / PwmToneOutput::SampleRateHz - 1;

volatile std::uint32_t &pwm(std::uintptr_t offset)
{
    return reg(timer::Timer3 + offset);
}

volatile std::uint32_t &sampler(std::uintptr_t offset)
{
    return reg(timer::Timer4 + offset);
}

} // namespace

void PwmToneOutput::start(unsigned irqPriority)
{
    reg(rcu::Apb1En) |= rcu::Timer3Clock | rcu::Timer4Clock;
    [[maybe_unused]] const std::uint32_t enabled = reg(rcu::Apb1En);

    // TIMER3: PWM mode 0 on channel 3 (high while the counter is below the
    // compare value), so duty 0 keeps the pin low.
    pwm(timer::Ctl0) = 0;
    pwm(timer::Psc) = 0;
    pwm(timer::Car) = PwmPeriod;
    pwm(timer::channelValue(ToneChannel)) = 0;
    pwm(timer::Chctl1) = (pwm(timer::Chctl1) & ~0xFF00u) | ((timer::PwmMode0 | timer::CompareShadow) << 8);
    pwm(timer::Chctl2) = (pwm(timer::Chctl2) & ~(0xFu << (4 * ToneChannel))) | timer::channelEnable(ToneChannel);
    pwm(timer::Swevg) = timer::UpdateEvent;
    pwm(timer::Ctl0) = timer::AutoReloadShadow | timer::CounterEnable;

    // TIMER4: an update interrupt per sample; its counter runs only while a
    // tone plays.
    sampler(timer::Ctl0) = 0;
    sampler(timer::Psc) = 0;
    sampler(timer::Car) = SamplePeriod;
    sampler(timer::Swevg) = timer::UpdateEvent;
    sampler(timer::Intf) = ~timer::UpdateInterrupt;
    sampler(timer::Dmainten) = timer::UpdateInterrupt;
    cortex_m::setIrqPriority(irq::Timer4, irqPriority, irq::PriorityBits);
    cortex_m::enableIrq(irq::Timer4);
}

void PwmToneOutput::play(std::uint32_t hz, std::uint8_t peak)
{
    m_synth.start(hz, SampleRateHz, peak);
    sampler(timer::Ctl0) = timer::CounterEnable;
}

void PwmToneOutput::stop()
{
    m_synth.stop();
}

void PwmToneOutput::playSamples(int (*source)())
{
    m_synth.stop();
    m_source = source;
    sampler(timer::Ctl0) = timer::CounterEnable;
}

void PwmToneOutput::onSampleInterrupt()
{
    sampler(timer::Intf) = ~timer::UpdateInterrupt;
    if (int (*const source)() = m_source; source != nullptr) {
        const int duty = source();
        if (duty >= 0) {
            pwm(timer::channelValue(ToneChannel)) = static_cast<std::uint32_t>(duty);
            return;
        }
        m_source = nullptr;
        pwm(timer::channelValue(ToneChannel)) = 0;
    }
    pwm(timer::channelValue(ToneChannel)) = m_synth.next();
    if (!m_synth.running()) {
        sampler(timer::Ctl0) = 0;
    }
}

} // namespace dmm::hal::gd32f30x
