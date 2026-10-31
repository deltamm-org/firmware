// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/ring_buffer.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::hal::gd32f30x {

/// The serial ports this driver knows.
enum class UsartPort : std::uint8_t
{
    Usart1, // PA2 sends, PA3 receives; on the 60 MHz APB1 clock
    Uart3,  // PC10 sends, PC11 receives; on the 60 MHz APB1 clock
};

/// An asynchronous serial port, 8 data bits, no parity, one stop bit.
/// Received bytes collect in a buffer from the receive interrupt; sending
/// waits for each byte. The board sets the pins up and routes the
/// interrupt to onInterrupt(). One task uses it.
class Usart
{
public:
    static constexpr std::size_t ReceiveBufferBytes = 512;

    explicit constexpr Usart(UsartPort port)
        : m_port(port)
    {
    }

    /// Starts the port at @p baud with its receive interrupt at
    /// @p irqPriority; drops anything received before. The interrupt does
    /// not call the kernel, so any priority will do.
    void start(std::uint32_t baud, unsigned irqPriority);
    /// Stops the port and its interrupt.
    void stop();

    /// Takes up to data.size() received bytes; returns how many.
    std::size_t read(std::span<std::uint8_t> data);
    /// Sends @p data, waiting until the last byte has left.
    void write(std::span<const std::uint8_t> data);

    /// Bytes lost because the buffer or the port's own register was full.
    std::uint32_t lostBytes() const { return m_lost; }

    /// Call from the port's interrupt handler.
    void onInterrupt();

private:
    volatile std::uint32_t &reg(std::uintptr_t offset) const;

    UsartPort m_port;
    bool m_running = false;
    lib::RingBuffer<std::uint8_t, ReceiveBufferBytes> m_received;
    volatile std::uint32_t m_lost = 0;
};

} // namespace dmm::hal::gd32f30x
