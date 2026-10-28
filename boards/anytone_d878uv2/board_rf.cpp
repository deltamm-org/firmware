// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_rf.h"

#include "pins.h"

#include <atomic>
#include <cmath>

#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/dac.h"
#include "dmm/hal/gd32f30x/exti.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/irq.h"
#include "dmm/hal/gd32f30x/pwm.h"
#include "dmm/hal/gd32f30x/step_timer.h"
#include "dmm/services/log.h"

namespace board {

namespace hal = dmm::hal::gd32f30x;

namespace cm = dmm::hal::cortex_m;

namespace {

hal::Timer2Pwm tunePwm{ReceiveTuneChannel};

// Digital transmit keying: the output control for a slot, 0 when not
// keying, and whether it is shaped. Written by the radio task, read by
// the slot interrupt.
std::atomic<std::uint16_t> slotPower{0};
std::atomic<bool> slotShaped{false};
// Slots keyed since keying started, for the log.
std::atomic<std::uint32_t> slotsKeyed{0};

// The shaped output at the higher power levels: after the slot line rises
// the output stays at 0 for a moment, then rises along an S-curve; after
// the line falls it holds a moment, then falls along an S-curve. The curve
// here is a raised cosine in RampSteps steps.
constexpr unsigned RampSteps = 64;
constexpr std::uint32_t RiseDelayUs = 180;
constexpr std::uint32_t RiseUs = 2300;
constexpr std::uint32_t FallHoldUs = 270;
constexpr std::uint32_t FallUs = 1000;
// The enables settle before the output moves, and the output is down
// before they drop.
constexpr unsigned EnableSettleUs = 4;
constexpr unsigned RampScaleBits = 12;
std::uint16_t rampCurve[RampSteps]; // 0..1 << RampScaleBits

hal::StepTimer rampTimer;

enum class Ramp : std::uint8_t
{
    Off,
    RiseDelay,
    Rising,
    On,
    FallHold,
    Falling,
};
// Only the slot and ramp interrupts touch these; they run at the same
// priority, so never inside each other.
Ramp ramp = Ramp::Off;
unsigned rampStep = 0;
std::uint16_t rampPower = 0;

void settle()
{
    cm::busyWaitCycles(EnableSettleUs * (hal::SystemClockHz / 1'000'000));
}

void enablesOn()
{
    hal::write(PowerAmplifierEnable, true);
    hal::write(TransmitEnable, true);
}

void enablesOff()
{
    hal::write(TransmitEnable, false);
    hal::write(PowerAmplifierEnable, false);
}

void output(unsigned step)
{
    hal::dac::write(PowerControlChannel,
        static_cast<std::uint16_t>((static_cast<std::uint32_t>(rampCurve[step]) * rampPower) >> RampScaleBits));
}

// Everything off at once: the ramp stopped, output 0, enables low.
void allOff()
{
    rampTimer.stop();
    ramp = Ramp::Off;
    hal::dac::write(PowerControlChannel, 0);
    enablesOff();
}

void slotOn(std::uint16_t power, bool shaped)
{
    slotsKeyed.fetch_add(1, std::memory_order_relaxed);
    rampPower = power;
    if (ramp == Ramp::Off) {
        enablesOn();
        settle();
    }
    if (!shaped) {
        rampTimer.stop();
        ramp = Ramp::On;
        hal::dac::write(PowerControlChannel, power);
        return;
    }
    if (ramp == Ramp::Falling || ramp == Ramp::FallHold) {
        // Back up from where the fall got to.
        ramp = Ramp::Rising;
        rampTimer.start(RiseUs / (RampSteps - 1));
        return;
    }
    if (ramp == Ramp::Off) {
        rampStep = 0;
        output(0);
        ramp = Ramp::RiseDelay;
        rampTimer.start(RiseDelayUs);
    }
}

void slotOff(bool shaped)
{
    if (!shaped || ramp == Ramp::Off) {
        hal::dac::write(PowerControlChannel, 0);
        settle();
        rampTimer.stop();
        ramp = Ramp::Off;
        enablesOff();
        return;
    }
    if (ramp == Ramp::On) {
        ramp = Ramp::FallHold;
        rampTimer.start(FallHoldUs);
        return;
    }
    // Still rising: down again from where it got to.
    ramp = Ramp::Falling;
    rampTimer.start(FallUs / (RampSteps - 1));
}

} // namespace

void BoardRf::configure()
{
    hal::write(TransmitEnable, false);
    hal::write(PowerAmplifierEnable, false);
    hal::configure(PowerAmplifierEnable, hal::PinMode::Output);
    hal::configure(PowerControl, hal::PinMode::Analog);
    hal::dac::start(PowerControlChannel, 0);
    hal::configure(ReceiveTune, hal::PinMode::Alternate, hal::PinSpeed::Medium);
    tunePwm.start(0, ReceiveTunePeriod);
    for (unsigned i = 0; i < RampSteps; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(RampSteps - 1);
        rampCurve[i] = static_cast<std::uint16_t>(
            std::lround((1.0F - std::cos(3.14159265F * x)) / 2.0F * static_cast<float>(1u << RampScaleBits)));
    }
    rampTimer.configure(TransmitSlotIrqPriority);
    hal::configure(TransmitSlot, hal::PinMode::InputPullDown);
    hal::enableExti(TransmitSlot, hal::Edge::Both);
    cm::setIrqPriority(hal::irq::Exti0, TransmitSlotIrqPriority, hal::irq::PriorityBits);
    cm::enableIrq(hal::irq::Exti0);
}

void BoardRf::setSlotKeying(std::uint16_t value, bool shaped)
{
    if (value == 0) {
        const bool wasKeying = slotPower.exchange(0, std::memory_order_relaxed) != 0;
        {
            const cm::InterruptLock lock;
            allOff();
        }
        if (wasKeying) {
            dmm::services::log::info("rf") << "slot keying off after " << slotsKeyed.load(std::memory_order_relaxed)
                                           << " slots";
        }
        return;
    }
    slotShaped.store(shaped, std::memory_order_relaxed);
    if (slotPower.exchange(value, std::memory_order_relaxed) == 0) {
        slotsKeyed.store(0, std::memory_order_relaxed);
    }
}

void BoardRf::onSlotEdge()
{
    if (!hal::takeExti(TransmitSlot.number)) {
        return;
    }
    const std::uint16_t power = slotPower.load(std::memory_order_relaxed);
    const bool shaped = slotShaped.load(std::memory_order_relaxed);
    if (power == 0) {
        allOff();
    } else if (hal::read(TransmitSlot)) {
        slotOn(power, shaped);
    } else {
        slotOff(shaped);
    }
}

void BoardRf::onRampStep()
{
    if (!rampTimer.takeInterrupt()) {
        return;
    }
    switch (ramp) {
    case Ramp::RiseDelay:
        ramp = Ramp::Rising;
        rampTimer.start(RiseUs / (RampSteps - 1));
        [[fallthrough]];
    case Ramp::Rising:
        if (rampStep + 1 < RampSteps) {
            output(++rampStep);
        }
        if (rampStep + 1 >= RampSteps) {
            rampTimer.stop();
            ramp = Ramp::On;
        }
        break;
    case Ramp::FallHold:
        ramp = Ramp::Falling;
        rampTimer.start(FallUs / (RampSteps - 1));
        [[fallthrough]];
    case Ramp::Falling:
        if (rampStep > 0) {
            output(--rampStep);
        }
        if (rampStep == 0) {
            hal::dac::write(PowerControlChannel, 0);
            settle();
            rampTimer.stop();
            ramp = Ramp::Off;
            enablesOff();
        }
        break;
    case Ramp::Off:
    case Ramp::On:
        rampTimer.stop();
        break;
    }
}

void BoardRf::setReferenceTrim(std::uint16_t value)
{
    if (!m_trimStarted) {
        hal::configure(ReferenceTrim, hal::PinMode::Analog);
        hal::dac::start(ReferenceTrimChannel, value);
        m_trimStarted = true;
        return;
    }
    hal::dac::write(ReferenceTrimChannel, value);
}

void BoardRf::setReceiveTune(std::uint16_t value)
{
    tunePwm.setDuty(value > ReceiveTunePeriod ? ReceiveTunePeriod + 1 : value);
}

void BoardRf::setTransmitPath(bool on)
{
    hal::write(TransmitEnable, on);
}

void BoardRf::setPowerAmplifier(bool on)
{
    hal::write(PowerAmplifierEnable, on);
}

void BoardRf::setPowerControl(std::uint16_t value)
{
    hal::dac::write(PowerControlChannel, value);
}

} // namespace board
