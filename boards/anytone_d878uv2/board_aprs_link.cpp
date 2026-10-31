// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/board/board_aprs_link.h"

#include "pins.h"

#include "dmm/hal/gd32f30x/usart.h"

namespace board {

namespace hal = dmm::hal::gd32f30x;
using hal::PinMode;

namespace {

hal::Usart usart{LinkBoardUsart};

} // namespace

void BoardAprsLink::configure()
{
    hal::configure(LinkBoardTx, PinMode::InputPullDown);
    hal::configure(LinkBoardRx, PinMode::InputPullDown);
}

void BoardAprsLink::start()
{
    hal::write(LinkBoardPower, true);
    hal::configure(LinkBoardRx, PinMode::InputPullUp);
    hal::configure(LinkBoardTx, PinMode::Alternate);
    usart.start(LinkBoardBaud, LinkBoardIrqPriority);
}

void BoardAprsLink::stop()
{
    usart.stop();
    configure();
    hal::write(LinkBoardPower, false);
}

std::size_t BoardAprsLink::read(std::span<std::uint8_t> data)
{
    return usart.read(data);
}

void BoardAprsLink::write(std::span<const std::uint8_t> data)
{
    usart.write(data);
}

std::uint32_t BoardAprsLink::lostBytes() const
{
    return usart.lostBytes();
}

void BoardAprsLink::onInterrupt()
{
    usart.onInterrupt();
}

} // namespace board
