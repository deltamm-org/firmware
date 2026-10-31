// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/hal/gd32f30x/usart.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

struct PortInfo
{
    std::uintptr_t base;
    std::uintptr_t clockEnableRegister;
    std::uint32_t clockEnableBit;
    std::uint32_t clockHz; // the bus clock the port runs from
    unsigned irq;
};

constexpr PortInfo info(UsartPort port)
{
    if (port == UsartPort::Uart3) {
        return {usart::Uart3, rcu::Apb1En, rcu::Uart3Clock, SystemClockHz / 2, irq::Uart3};
    }
    return {usart::Usart1, rcu::Apb1En, rcu::Usart1Clock, SystemClockHz / 2, irq::Usart1};
}

} // namespace

volatile std::uint32_t &Usart::reg(std::uintptr_t offset) const
{
    return gd32f30x::reg(info(m_port).base + offset);
}

void Usart::start(std::uint32_t baud, unsigned irqPriority)
{
    const PortInfo port = info(m_port);
    gd32f30x::reg(port.clockEnableRegister) |= port.clockEnableBit;
    [[maybe_unused]] const std::uint32_t enabled = gd32f30x::reg(port.clockEnableRegister);

    cortex_m::disableIrq(port.irq);
    reg(usart::Ctl0) = 0;
    {
        cortex_m::InterruptLock lock;
        m_received.clear();
    }
    // 16 times oversampling: the divider is the bus clock over the baud
    // rate, with 4 fraction bits, which is the register value as it stands.
    reg(usart::Baud) = (port.clockHz + baud / 2) / baud;
    reg(usart::Ctl1) = 0; // one stop bit
    reg(usart::Ctl2) = 0; // no flow control
    reg(usart::Ctl0) = usart::Enable | usart::TransmitEnable | usart::ReceiveEnable | usart::ReceiveInterrupt;
    // Drop a byte or error left from before.
    [[maybe_unused]] const std::uint32_t stat = reg(usart::Stat0);
    [[maybe_unused]] const std::uint32_t data = reg(usart::Data);
    cortex_m::setIrqPriority(port.irq, irqPriority, irq::PriorityBits);
    cortex_m::enableIrq(port.irq);
    m_running = true;
}

void Usart::stop()
{
    cortex_m::disableIrq(info(m_port).irq);
    reg(usart::Ctl0) = 0;
    m_running = false;
}

std::size_t Usart::read(std::span<std::uint8_t> data)
{
    cortex_m::InterruptLock lock;
    std::size_t n = 0;
    while (n < data.size()) {
        const auto byte = m_received.pop();
        if (!byte) {
            break;
        }
        data[n++] = *byte;
    }
    return n;
}

void Usart::write(std::span<const std::uint8_t> data)
{
    if (!m_running) {
        return;
    }
    for (std::uint8_t byte : data) {
        while ((reg(usart::Stat0) & usart::TransmitEmpty) == 0) {
        }
        reg(usart::Data) = byte;
    }
    while ((reg(usart::Stat0) & usart::TransmitComplete) == 0) {
    }
}

void Usart::onInterrupt()
{
    const std::uint32_t stat = reg(usart::Stat0);
    if ((stat & (usart::ReceiveNotEmpty | usart::OverrunError)) == 0) {
        return;
    }
    const auto byte = static_cast<std::uint8_t>(reg(usart::Data));
    if ((stat & usart::OverrunError) != 0) {
        m_lost = m_lost + 1;
    }
    if (!m_received.push(byte)) {
        m_lost = m_lost + 1;
    }
}

} // namespace dmm::hal::gd32f30x
