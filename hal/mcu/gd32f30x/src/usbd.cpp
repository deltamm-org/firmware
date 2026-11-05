// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/usbd.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

namespace cm = cortex_m;

constexpr std::uint16_t TableBytes = 64; // 8 descriptors of 4 halfwords
constexpr std::uint16_t PacketMemoryBytes = 512;
constexpr std::uint16_t Ep0PacketSize = 64;

// Packet memory is addressed in 16-bit words, each at a 32-bit stride.
volatile std::uint32_t &packetWord(std::uint16_t byteOffset)
{
    return reg(usbd::PacketMemory + 2u * byteOffset);
}

// Buffer descriptor fields of endpoint n: TX address and count, RX address
// and count.
volatile std::uint32_t &txAddress(unsigned n)
{
    return packetWord(static_cast<std::uint16_t>(8 * n));
}
volatile std::uint32_t &txCount(unsigned n)
{
    return packetWord(static_cast<std::uint16_t>(8 * n + 2));
}
volatile std::uint32_t &rxAddress(unsigned n)
{
    return packetWord(static_cast<std::uint16_t>(8 * n + 4));
}
volatile std::uint32_t &rxCount(unsigned n)
{
    return packetWord(static_cast<std::uint16_t>(8 * n + 6));
}

// RX byte-count register value that allocates @p bytes: 2-byte blocks up
// to 62 bytes, else 32-byte blocks (block count minus one).
std::uint32_t rxAllocation(std::uint16_t bytes)
{
    if (bytes > 62) {
        return (1u << 15) | ((static_cast<std::uint32_t>(bytes) / 32u - 1u) << 10);
    }
    return (static_cast<std::uint32_t>(bytes) / 2u) << 10;
}

volatile std::uint32_t &endpointReg(unsigned n)
{
    return reg(usbd::endpoint(n));
}

// Writes leave RX_ST and TX_ST as they are and toggle nothing, unless
// asked: these helpers keep the special write semantics in one place.
constexpr std::uint32_t KeepFlags = usbd::RxDone | usbd::TxDone;

void setTxStatus(unsigned n, std::uint32_t status)
{
    const std::uint32_t v = endpointReg(n);
    endpointReg(n) = (v & usbd::PlainBits) | KeepFlags | ((v & usbd::TxStatusMask) ^ (status << 4));
}

void setRxStatus(unsigned n, std::uint32_t status)
{
    const std::uint32_t v = endpointReg(n);
    endpointReg(n) = (v & usbd::PlainBits) | KeepFlags | ((v & usbd::RxStatusMask) ^ (status << 12));
}

void clearRxDone(unsigned n)
{
    endpointReg(n) = (endpointReg(n) & usbd::PlainBits) | usbd::TxDone;
}

void clearTxDone(unsigned n)
{
    endpointReg(n) = (endpointReg(n) & usbd::PlainBits) | usbd::RxDone;
}

// Sets both data toggles to DATA0.
void resetToggles(unsigned n)
{
    const std::uint32_t v = endpointReg(n);
    endpointReg(n) = (v & usbd::PlainBits) | KeepFlags | (v & (usbd::RxToggle | usbd::TxToggle));
}

// Clears both handshake states (disabled), both toggles and both done
// flags, and the type and address.
void disable(unsigned n)
{
    endpointReg(n) = endpointReg(n) & (usbd::RxToggle | usbd::RxStatusMask | usbd::TxToggle | usbd::TxStatusMask);
}

void clearInterruptFlag(std::uint32_t flag)
{
    reg(usbd::Intf) = ~flag & 0xFFFFu;
}

} // namespace

Usbd::Usbd(Pin pullUp, unsigned irqPriority)
    : m_pullUp(pullUp), m_irqPriority(irqPriority)
{
}

void Usbd::start(UsbDeviceEvents &events)
{
    m_events = &events;
    gd32f30x::write(m_pullUp, false);
    configure(m_pullUp, PinMode::Output);

    reg(rcu::AddCtl) &= ~rcu::Usb48mFromIrc48m; // 48 MHz from the PLL divider
    reg(rcu::Apb1En) |= rcu::UsbdClock;
    [[maybe_unused]] const std::uint32_t enabled = reg(rcu::Apb1En);

    // Power up the transceiver and give it time to settle (over 1 us).
    reg(usbd::Ctl) = usbd::ForceReset;
    for (int i = 0; i < 1000; ++i) {
        asm volatile("nop");
    }
    reg(usbd::Ctl) = 0;
    reg(usbd::Intf) = 0;
    reg(usbd::Baddr) = 0;
    reg(usbd::Ctl) = usbd::TransferIrq | usbd::ResetIrq | usbd::SuspendIrq | usbd::WakeupIrq;

    cm::setIrqPriority(irq::UsbdLowPriority, m_irqPriority, irq::PriorityBits);
    cm::enableIrq(irq::UsbdLowPriority);
    gd32f30x::write(m_pullUp, true);
}

void Usbd::resetEndpoints()
{
    for (unsigned n = 0; n < EndpointCount; ++n) {
        disable(n);
        txAddress(n) = 0;
        txCount(n) = 0;
        rxAddress(n) = 0;
        rxCount(n) = 0;
    }
    m_nextFree = TableBytes;
    m_txBuffer[0] = allocate(Ep0PacketSize);
    m_rxBuffer[0] = allocate(Ep0PacketSize);
    txAddress(0) = m_txBuffer[0];
    rxAddress(0) = m_rxBuffer[0];
    rxCount(0) = rxAllocation(Ep0PacketSize);
    endpointReg(0) = usbd::TypeControl;
    setRxStatus(0, usbd::Valid);
    setTxStatus(0, usbd::Nak);
    reg(usbd::Daddr) = usbd::Enable;
}

std::uint16_t Usbd::allocate(std::uint16_t bytes)
{
    const std::uint16_t at = m_nextFree;
    m_nextFree = static_cast<std::uint16_t>(m_nextFree + ((bytes + 1u) & ~1u));
    if (m_nextFree > PacketMemoryBytes) {
        m_nextFree = at;
        return 0;
    }
    return at;
}

void Usbd::setAddress(std::uint8_t address)
{
    reg(usbd::Daddr) = usbd::Enable | (address & 0x7Fu);
}

void Usbd::closeEndpoints()
{
    for (unsigned n = 1; n < EndpointCount; ++n) {
        disable(n);
    }
    m_nextFree = static_cast<std::uint16_t>(TableBytes + 2 * Ep0PacketSize);
}

void Usbd::openEndpoint(std::uint8_t address, UsbEndpointType type, std::uint16_t maxPacket)
{
    const unsigned n = address & 0xFu;
    if (n == 0 || n >= EndpointCount) {
        return;
    }
    const std::uint16_t buffer = allocate(maxPacket);
    if (buffer == 0) {
        return;
    }
    const std::uint32_t typeBits = type == UsbEndpointType::Interrupt ? usbd::TypeInterrupt : usbd::TypeBulk;
    endpointReg(n) = KeepFlags | typeBits | n;
    resetToggles(n);
    if ((address & 0x80u) != 0) {
        m_txBuffer[n] = buffer;
        txAddress(n) = buffer;
        txCount(n) = 0;
        setTxStatus(n, usbd::Nak);
    } else {
        m_rxBuffer[n] = buffer;
        rxAddress(n) = buffer;
        rxCount(n) = rxAllocation(maxPacket);
        setRxStatus(n, usbd::Valid);
    }
}

std::size_t Usbd::read(std::uint8_t address, std::uint8_t *data, std::size_t capacity)
{
    const unsigned n = address & 0xFu;
    std::size_t length = rxCount(n) & 0x3FFu;
    if (length > capacity) {
        length = capacity;
    }
    for (std::size_t i = 0; i < length; i += 2) {
        const std::uint32_t word = packetWord(static_cast<std::uint16_t>(m_rxBuffer[n] + i));
        data[i] = static_cast<std::uint8_t>(word);
        if (i + 1 < length) {
            data[i + 1] = static_cast<std::uint8_t>(word >> 8);
        }
    }
    return length;
}

void Usbd::armOut(std::uint8_t address)
{
    setRxStatus(address & 0xFu, usbd::Valid);
}

void Usbd::write(std::uint8_t address, const std::uint8_t *data, std::size_t length)
{
    const unsigned n = address & 0xFu;
    for (std::size_t i = 0; i < length; i += 2) {
        std::uint32_t word = data[i];
        if (i + 1 < length) {
            word |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        }
        packetWord(static_cast<std::uint16_t>(m_txBuffer[n] + i)) = word;
    }
    txCount(n) = static_cast<std::uint32_t>(length);
    setTxStatus(n, usbd::Valid);
}

void Usbd::stall(std::uint8_t address)
{
    const unsigned n = address & 0xFu;
    if (n == 0) {
        setTxStatus(0, usbd::Stall);
        setRxStatus(0, usbd::Valid); // the next SETUP is always accepted
    } else if ((address & 0x80u) != 0) {
        setTxStatus(n, usbd::Stall);
    } else {
        setRxStatus(n, usbd::Stall);
    }
}

void Usbd::lock()
{
    cm::disableIrq(irq::UsbdLowPriority);
    cm::dataSyncBarrier();
    cm::instructionSyncBarrier();
    ++m_lockDepth;
}

void Usbd::unlock()
{
    if (--m_lockDepth == 0) {
        cm::enableIrq(irq::UsbdLowPriority);
    }
}

void Usbd::handleInterrupt()
{
    std::uint32_t flags = reg(usbd::Intf);
    if ((flags & usbd::ResetFlag) != 0) {
        clearInterruptFlag(usbd::ResetFlag);
        reg(usbd::Ctl) &= ~usbd::SetSuspend;
        resetEndpoints();
        m_events->onBusReset();
        return;
    }
    while (((flags = reg(usbd::Intf)) & usbd::TransferFlag) != 0) {
        const unsigned n = flags & usbd::EndpointMask;
        const std::uint32_t cs = endpointReg(n);
        if ((cs & usbd::RxDone) != 0) {
            if (n == 0 && (cs & usbd::Setup) != 0) {
                std::uint8_t packet[8]{};
                read(0, packet, sizeof packet);
                clearRxDone(0);
                m_events->onSetup(packet);
            } else {
                clearRxDone(n);
                m_events->onOut(static_cast<std::uint8_t>(n));
            }
        }
        if ((cs & usbd::TxDone) != 0) {
            clearTxDone(n);
            m_events->onInComplete(static_cast<std::uint8_t>(0x80u | n));
        }
    }
    if ((flags & usbd::SuspendFlag) != 0) {
        clearInterruptFlag(usbd::SuspendFlag);
        reg(usbd::Ctl) |= usbd::SetSuspend;
        m_events->onSuspend();
    }
    if ((flags & usbd::WakeupFlag) != 0) {
        clearInterruptFlag(usbd::WakeupFlag);
        reg(usbd::Ctl) &= ~usbd::SetSuspend;
    }
}

} // namespace dmm::hal::gd32f30x
