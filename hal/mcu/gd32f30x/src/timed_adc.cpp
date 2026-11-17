// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/hal/gd32f30x/timed_adc.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

constexpr unsigned DmaChannel = 4; // DMA1's channel for ADC2 requests
constexpr unsigned TriggerChannel = 2;

// A conversion takes 239.5 + 12.5 ADC clocks at the longest sample time;
// the ADC clock is the system clock / 8.
constexpr std::uint32_t ConversionClocks = 252 * 8;

// Bounds every wait on the ADC, so a fault cannot hang the caller.
constexpr std::uint32_t MaxPolls = 100'000;

volatile std::uint32_t &adc2(std::uintptr_t offset)
{
    return reg(adc::Adc2 + offset);
}

volatile std::uint32_t &dma1(std::uintptr_t offset)
{
    return reg(dma::Dma1 + offset);
}

volatile std::uint32_t &timer0(std::uintptr_t offset)
{
    return reg(timer::Timer0 + offset);
}

void waitWhileSet(std::uint32_t bit)
{
    for (std::uint32_t i = 0; i < MaxPolls && (adc2(adc::Ctl1) & bit) != 0; ++i) {
    }
}

} // namespace

bool TimedAdc::start(unsigned channel, std::uint32_t rateHz, std::span<std::uint16_t> ring)
{
    if (rateHz == 0 || ring.empty() || ring.size() > 0xFFFF || channel > 17) {
        return false;
    }
    const std::uint32_t period = (SystemClockHz + rateHz / 2) / rateHz;
    if (period <= ConversionClocks || period > 0x10000) {
        return false;
    }
    stop();
    reg(rcu::AhbEn) |= rcu::Dma1Clock;
    reg(rcu::Apb2En) |= rcu::Adc2Clock | rcu::Timer0Clock;
    [[maybe_unused]] const std::uint32_t enabled = reg(rcu::Apb2En);

    // One regular conversion of the channel, at the longest sample time.
    adc2(adc::Ctl0) = 0;
    const unsigned field = channel < 10 ? 3 * channel : 3 * (channel - 10);
    const std::uintptr_t sampt = channel < 10 ? adc::Sampt1 : adc::Sampt0;
    adc2(sampt) = (adc2(sampt) & ~(7u << field)) | (adc::LongestSample << field);
    adc2(adc::Rsq0) = 0;
    adc2(adc::Rsq2) = channel;

    adc2(adc::Ctl1) = adc::On;
    cortex_m::busyWaitCycles(SystemClockHz / 100'000); // 10 us to power up
    adc2(adc::Ctl1) = adc::On | adc::ResetCalibration;
    waitWhileSet(adc::ResetCalibration);
    adc2(adc::Ctl1) = adc::On | adc::Calibrate;
    waitWhileSet(adc::Calibrate);

    dma1(dma::channelCtl(DmaChannel)) = 0;
    dma1(dma::Intc) = dma::clearFlags(DmaChannel);
    dma1(dma::channelPeripheral(DmaChannel)) = static_cast<std::uint32_t>(adc::Adc2 + adc::Rdata);
    dma1(dma::channelMemory(DmaChannel)) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ring.data()));
    dma1(dma::channelCount(DmaChannel)) = static_cast<std::uint32_t>(ring.size());
    dma1(dma::channelCtl(DmaChannel)) =
        dma::Peripheral16Bit | dma::Memory16Bit | dma::MemoryIncrement | dma::Circular | dma::PriorityHigh | dma::ChannelEnable;
    m_size = ring.size();

    // Changing other bits along with ADCON does not start a conversion.
    adc2(adc::Ctl1) = adc::On | adc::DmaRequest | adc::TriggerEnable | adc::Adc2TriggerTimer0Ch2;

    // PWM mode 0 on channel 2: one rising edge per period, mid-period
    // compare. The channel and primary outputs are enabled so the compare
    // event is generated; its pin carries it only in an alternate mode.
    timer0(timer::Ctl0) = 0;
    timer0(timer::Psc) = 0;
    timer0(timer::Car) = period - 1;
    timer0(timer::channelValue(TriggerChannel)) = period / 2;
    timer0(timer::Chctl1) = (timer::PwmMode0 | timer::CompareShadow);
    timer0(timer::Chctl2) = timer::channelEnable(TriggerChannel);
    timer0(timer::Cchp) = timer::PrimaryOutputEnable;
    timer0(timer::Cnt) = 0;
    timer0(timer::Swevg) = timer::UpdateEvent;
    timer0(timer::Intf) = 0;
    timer0(timer::Ctl0) = timer::AutoReloadShadow | timer::CounterEnable;
    return true;
}

void TimedAdc::stop()
{
    if (m_size == 0) {
        return;
    }
    timer0(timer::Ctl0) = 0;
    timer0(timer::Cchp) = 0;
    timer0(timer::Chctl2) = 0;
    adc2(adc::Ctl1) = 0;
    dma1(dma::channelCtl(DmaChannel)) = 0;
    dma1(dma::Intc) = dma::clearFlags(DmaChannel);
    reg(rcu::Apb2En) &= ~(rcu::Adc2Clock | rcu::Timer0Clock);
    reg(rcu::AhbEn) &= ~rcu::Dma1Clock;
    m_size = 0;
}

std::size_t TimedAdc::position() const
{
    if (m_size == 0) {
        return 0;
    }
    const std::size_t left = dma1(dma::channelCount(DmaChannel)) & 0xFFFFu;
    return left == 0 || left > m_size ? 0 : m_size - left;
}

} // namespace dmm::hal::gd32f30x
