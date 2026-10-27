// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/hal/gd32f30x/bitbang_spi_nand_bus.h"

#include "dmm/hal/gd32f30x/registers.h"

namespace dmm::hal::gd32f30x {

namespace {

// The chip drives its outputs after the falling clock edge; a few cycles
// pass before they are sampled.
inline void settle()
{
    asm volatile("nop\n\tnop\n\tnop\n\tnop");
}

} // namespace

namespace {

volatile std::uint32_t *portRegister(Pin pin, std::uintptr_t offset)
{
    return &reg(gpio::base(static_cast<unsigned>(pin.port)) + offset);
}

} // namespace

BitBangSpiNandBus::BitBangSpiNandBus(const Pins &pins)
    : m_pins(pins), m_clockBop(portRegister(pins.clock, gpio::Bop)), m_dataBop(portRegister(pins.dataIn, gpio::Bop)),
      m_dataInState(portRegister(pins.dataIn, gpio::Istat)), m_dataOutState(portRegister(pins.dataOut, gpio::Istat)),
      m_clockMask(1u << pins.clock.number), m_dataInMask(1u << pins.dataIn.number), m_dataOutMask(1u << pins.dataOut.number),
      m_clockAndDataShared(pins.clock.port == pins.dataIn.port)
{
}

void BitBangSpiNandBus::configurePins()
{
    gd32f30x::write(m_pins.chipSelect, true);
    configure(m_pins.chipSelect, PinMode::Output, PinSpeed::Fast);
    gd32f30x::write(m_pins.clock, true);
    configure(m_pins.clock, PinMode::Output, PinSpeed::Fast);
    gd32f30x::write(m_pins.dataIn, true);
    configure(m_pins.dataIn, PinMode::Output, PinSpeed::Fast);
    configure(m_pins.dataOut, PinMode::InputPullUp);
}

void BitBangSpiNandBus::select()
{
    gd32f30x::write(m_pins.chipSelect, false);
}

void BitBangSpiNandBus::deselect()
{
    gd32f30x::write(m_pins.chipSelect, true);
}

// The chip takes each bit on the rising clock edge. With the clock and
// data-in on one port, one store drops the clock and sets the bit.
void BitBangSpiNandBus::write(const std::uint8_t *bytes, std::size_t count)
{
    const std::uint32_t clockLow = m_clockMask << 16;
    const std::uint32_t dataHigh = m_dataInMask;
    const std::uint32_t dataLow = m_dataInMask << 16;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t b = bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t data = (b & 0x80u) != 0 ? dataHigh : dataLow;
            if (m_clockAndDataShared) {
                *m_clockBop = clockLow | data;
            } else {
                *m_clockBop = clockLow;
                *m_dataBop = data;
            }
            *m_clockBop = m_clockMask;
            b = static_cast<std::uint8_t>(b << 1);
        }
    }
}

void BitBangSpiNandBus::read(std::uint8_t *bytes, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        unsigned b = 0;
        for (int bit = 0; bit < 8; ++bit) {
            *m_clockBop = m_clockMask << 16;
            settle();
            b = (b << 1) | ((*m_dataOutState & m_dataOutMask) != 0 ? 1u : 0u);
            *m_clockBop = m_clockMask;
        }
        bytes[i] = static_cast<std::uint8_t>(b);
    }
}

// Each clock carries two bits, the higher one on the chip's data-out line.
void BitBangSpiNandBus::readDual(std::uint8_t *bytes, std::size_t count)
{
    configure(m_pins.dataIn, PinMode::InputPullUp);
    for (std::size_t i = 0; i < count; ++i) {
        unsigned b = 0;
        for (int pair = 0; pair < 4; ++pair) {
            *m_clockBop = m_clockMask << 16;
            settle();
            b = (b << 2) | ((*m_dataOutState & m_dataOutMask) != 0 ? 2u : 0u) | ((*m_dataInState & m_dataInMask) != 0 ? 1u : 0u);
            *m_clockBop = m_clockMask;
        }
        bytes[i] = static_cast<std::uint8_t>(b);
    }
    configure(m_pins.dataIn, PinMode::Output, PinSpeed::Fast);
}

} // namespace dmm::hal::gd32f30x
