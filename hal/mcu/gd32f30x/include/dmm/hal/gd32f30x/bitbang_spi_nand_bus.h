// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/spi_nand_bus.h"

namespace dmm::hal::gd32f30x {

/// SPI mode 0 on four GPIO pins. For dual reads the data-in pin turns
/// into a second input for the time of the read.
class BitBangSpiNandBus final : public SpiNandBus
{
public:
    struct Pins
    {
        Pin chipSelect; // low = selected
        Pin clock;
        Pin dataIn;  // to the chip; IO0 in dual mode
        Pin dataOut; // from the chip; IO1 in dual mode
    };

    explicit BitBangSpiNandBus(const Pins &pins);

    void configurePins();

    void select() override;
    void deselect() override;
    void write(const std::uint8_t *bytes, std::size_t count) override;
    void read(std::uint8_t *bytes, std::size_t count) override;
    void readDual(std::uint8_t *bytes, std::size_t count) override;

private:
    Pins m_pins;
    // The bit set/clear registers of the clock and data-in ports, the
    // input registers of the two data lines, and the pin masks, so each
    // clock is one or two stores. Set/clear words: low half sets, high
    // half clears.
    volatile std::uint32_t *m_clockBop;
    volatile std::uint32_t *m_dataBop;
    const volatile std::uint32_t *m_dataInState;
    const volatile std::uint32_t *m_dataOutState;
    std::uint32_t m_clockMask;
    std::uint32_t m_dataInMask;
    std::uint32_t m_dataOutMask;
    bool m_clockAndDataShared;
};

} // namespace dmm::hal::gd32f30x
