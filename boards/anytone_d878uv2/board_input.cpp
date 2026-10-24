// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_input.h"

#include "board_adc.h"

#include "panel_bus.h"
#include "pins.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/exti.h"
#include "dmm/hal/gd32f30x/irq.h"

#include <atomic>
#include <iterator>

namespace board {

namespace hal = dmm::hal::gd32f30x;
namespace cm = dmm::hal::cortex_m;
using dmm::services::Key;
using dmm::services::keyBit;
using dmm::services::KeyMask;

namespace {

std::atomic<std::int32_t> encoderCount{0};

constexpr std::uint16_t RowMask = 0xFFu << KeypadFirstRow;
// Time for a column to follow a row through its pull-up.
constexpr std::uint32_t SettleCycles = hal::SystemClockHz / 200'000; // 5 us

// Keys by row (PE7 first) and column (PC5, PB1, PB2); Count = no key.
constexpr Key Matrix[8][3] = {
    {Key::Digit1, Key::Menu, Key::Front1},
    {Key::Digit2, Key::Up, Key::Front2},
    {Key::Digit3, Key::Down, Key::Count},
    {Key::Digit4, Key::Digit7, Key::Count},
    {Key::Digit5, Key::Digit8, Key::Count},
    {Key::Digit6, Key::Digit9, Key::Count},
    {Key::Digit0, Key::Hash, Key::Count},
    {Key::Star, Key::Exit, Key::Count},
};

bool low(Pin pin)
{
    return !hal::read(pin);
}

} // namespace

void BoardInput::configure()
{
    for (Pin pin : KeypadColumns) {
        hal::configure(pin, hal::PinMode::InputPullUp);
    }
    for (Pin pin : {Ptt, Pf1, Pf2, EncoderA, EncoderB}) {
        hal::configure(pin, hal::PinMode::InputPullUp);
    }
    hal::configure(VolumeKnob, hal::PinMode::Analog);

    hal::enableExti(EncoderA, hal::Edge::Both);
    cm::setIrqPriority(hal::irq::Exti5To9, EncoderIrqPriority, hal::irq::PriorityBits);
    cm::enableIrq(hal::irq::Exti5To9);
}

KeyMask BoardInput::readKeys()
{
    KeyMask keys = 0;
    if (low(Ptt)) {
        keys |= keyBit(Key::Ptt);
    }
    if (low(Pf1)) {
        keys |= keyBit(Key::Side1);
    }
    if (low(Pf2)) {
        keys |= keyBit(Key::Side2);
    }

    PanelPinsGuard guard;
    hal::writePins(KeypadRowPort, RowMask, 0);
    cm::busyWaitCycles(SettleCycles);
    const bool pf3 = low(Pf3Column);
    if (pf3) {
        keys |= keyBit(Key::Side3);
    }
    for (unsigned row = 0; row < 8; ++row) {
        const auto rowBit = static_cast<std::uint16_t>(1u << (KeypadFirstRow + row));
        hal::writePins(KeypadRowPort, static_cast<std::uint16_t>(RowMask & ~rowBit), rowBit);
        cm::busyWaitCycles(SettleCycles);
        // With PF3 down the third column is low on every row: skip it.
        const unsigned columns = pf3 ? 2 : 3;
        for (unsigned col = 0; col < columns; ++col) {
            const Key key = Matrix[row][col];
            if (key != Key::Count && low(KeypadColumns[col])) {
                keys |= keyBit(key);
            }
        }
    }
    hal::writePins(KeypadRowPort, RowMask, 0);
    return keys;
}

std::int32_t BoardInput::encoderPosition()
{
    return encoderCount.load(std::memory_order_relaxed);
}

int BoardInput::encoderCountsPerDetent() const
{
    return EncoderCountsPerDetent;
}

int BoardInput::volumeKnob()
{
    return static_cast<int>(adc().read(VolumeKnobChannel)) * 1000 / 4095;
}

// On each edge of A, B tells the direction: different from A after the edge
// when turned clockwise, equal when turned the other way. A bouncing A edge
// adds and takes back the same count.
void BoardInput::onEncoderEdge()
{
    if (!hal::takeExti(EncoderA.number)) {
        return;
    }
    const bool a = hal::read(EncoderA);
    const bool b = hal::read(EncoderB);
    encoderCount.fetch_add(a != b ? 1 : -1, std::memory_order_relaxed);
}

std::string_view BoardInput::keyLabel(dmm::services::Key key) const
{
    static constexpr std::string_view Labels[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#", "Menu",
        "Exit", "Up", "Down", "P1", "P2", "PF1", "PF2", "PF3", "PTT"};
    static_assert(std::size(Labels) == static_cast<unsigned>(dmm::services::Key::Count));
    const auto i = static_cast<unsigned>(key);
    return i < std::size(Labels) ? Labels[i] : "?";
}

} // namespace board
