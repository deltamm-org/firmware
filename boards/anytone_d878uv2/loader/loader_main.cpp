// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

// DeltaMM's loader. At every start it reads the boot record, finishes a
// swap in progress and decides which image to start, all at the reset
// clock. In loader mode it starts the PLL and USB and serves the loader
// protocol on one serial port; it then always ends with a restart, so the
// app starts from a reset state.

#include "../handover.h"
#include "../image_info.h"
#include "../mcu_flash.h"
#include "../pins.h"

#include "dmm/drivers/usb/cdc_acm.h"
#include "dmm/drivers/usb/descriptors.h"
#include "dmm/drivers/usb/device.h"
#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/registers.h"
#include "dmm/hal/gd32f30x/reset.h"
#include "dmm/hal/gd32f30x/startup.h"
#include "dmm/hal/gd32f30x/usbd.h"
#include "dmm/hal/gd32f30x/watchdog.h"
#include "dmm/services/boot_loader.h"
#include "dmm/services/d878uv2_boot_layout.h"
#include "dmm/services/loader_protocol.h"
#include "dmm/version.h"

#include <array>
#include <cstdint>
#include <span>

DMM_IMAGE_INFO(dmm::services::boot::ImageKind::Loader);

namespace {

namespace hal = dmm::hal::gd32f30x;
namespace cm = dmm::hal::cortex_m;
namespace boot = dmm::services::boot;
namespace layout = dmm::services::d878uv2;
namespace usb = dmm::drivers::usb;
using hal::Pin;
using hal::PinMode;

/// Watchdog while the loader runs and until the app sets its own.
constexpr std::uint32_t LoaderWatchdogMs = 20'000;
/// Loader mode ends by itself after this long without the PC, when there
/// is an image to start.
constexpr std::uint32_t IdleRestartMs = 60'000;
/// Time for the last reply to leave before a restart.
constexpr std::uint32_t ReplyDrainMs = 100;

/// The clock the MCU starts on (its internal RC oscillator).
constexpr std::uint32_t ResetClockHz = 8'000'000;

// Held at power-on: stay in loader mode.
constexpr Pin ForceKeys[] = {board::Ptt, board::Pf1};

DMM_HANDOVER_BLOCK(handover);

class Events final : public boot::LoaderEvents
{
public:
    void busy(bool on) override
    {
        // Keeps the radio on while flash is written, even with the knob off.
        if (on) {
            hal::write(board::PowerHold, true);
        }
        hal::write(board::RedLed, on);
        if (!on) {
            hal::write(board::PowerHold, false);
        }
        hal::feedWatchdog();
    }
    void alive() override { hal::feedWatchdog(); }
};

Events events;
board::McuFlash mcuFlash{{layout::SlotABase, layout::RecordEnd}};
boot::GuardedFlash flash{mcuFlash, layout::LoaderWritable};
std::array<std::uint8_t, boot::SectorBytes> sectorBuffer;
boot::Loader loader{flash, layout::BootLayout, sectorBuffer, events};

// One serial port; the radio's USB IDs, with its own product and serial
// number strings, so tools tell it apart from the app and update mode.
constexpr auto DeviceDescriptor = usb::compositeDeviceDescriptor({0x28E9, 0x018A, 0x0001, 1, 2, 4});
constexpr auto Port = usb::cdcAcmDescriptors(0, 0x82, 0x01, 0x81, 3);
constexpr auto ConfigurationDescriptor = usb::join(usb::configurationHeader(9 + usb::CdcAcmDescriptorBytes, 2, 50), Port);
constexpr auto Language = usb::languageDescriptor();
constexpr auto Manufacturer = usb::stringDescriptor("DeltaMM");
constexpr auto Product = usb::stringDescriptor("DeltaMM loader");
constexpr auto PortName = usb::stringDescriptor("DeltaMM loader");
constexpr auto SerialNumber = usb::stringDescriptor("DeltaMM-loader");
constexpr std::span<const std::uint8_t> Strings[] = {Language, Manufacturer, Product, PortName, SerialNumber};

hal::Usbd usbd{board::UsbPullUp, board::UsbIrqPriority};
usb::CdcAcm port{0, 0x82, 0x01, 0x81};
usb::Function *const usbFunctions[] = {&port};
usb::Device usbDevice{usbd, usb::Descriptors{DeviceDescriptor, ConfigurationDescriptor, Strings}, usbFunctions};

// Milliseconds from the cycle counter at 120 MHz, kept over its wrap.
class Clock
{
public:
    Clock()
    {
        cm::enableCycleCounter();
        m_last = cm::reg(cm::dwt::Cyccnt);
    }
    std::uint32_t nowMs()
    {
        const std::uint32_t cycles = cm::reg(cm::dwt::Cyccnt);
        m_rest += cycles - m_last;
        m_last = cycles;
        m_ms += m_rest / (hal::SystemClockHz / 1000);
        m_rest %= hal::SystemClockHz / 1000;
        return m_ms;
    }

private:
    std::uint32_t m_last = 0;
    std::uint32_t m_rest = 0;
    std::uint32_t m_ms = 0;
};

bool forceKeysHeld()
{
    for (Pin key : ForceKeys) {
        hal::configure(key, PinMode::InputPullUp);
    }
    // Several reads over about 20 ms at the 8 MHz reset clock; low = held.
    cm::enableCycleCounter();
    for (int i = 0; i < 10; ++i) {
        cm::busyWaitCycles(ResetClockHz / 500);
        for (Pin key : ForceKeys) {
            if (hal::read(key)) {
                return false;
            }
        }
    }
    return true;
}

bool somethingToStart()
{
    const boot::BootState &s = loader.record().state();
    return s.a.state == boot::SlotState::Confirmed || s.a.state == boot::SlotState::Trial ||
        s.b.state == boot::SlotState::Committed || s.b.state == boot::SlotState::Confirmed;
}

void writeAll(std::span<const std::uint8_t> bytes, Clock &clock)
{
    const std::uint32_t start = clock.nowMs();
    while (!bytes.empty() && port.isOpen() && clock.nowMs() - start < 500) {
        bytes = bytes.subspan(port.write(bytes));
    }
}

[[noreturn]] void restart(Clock &clock, const boot::LoaderProtocol::Restart &r)
{
    const std::uint32_t start = clock.nowMs();
    while (clock.nowMs() - start < ReplyDrainMs) {
    }
    if (r.request == boot::Request::Rollback) {
        board::storeHandover(handover, boot::makeHandover(boot::HandoverRequest::Rollback, 0));
    } else if (r.swapCut != 0) {
        board::storeHandover(handover, boot::makeHandover(boot::HandoverRequest::SwapCut, r.swapCut));
    }
    cm::systemReset();
}

[[noreturn]] void loaderMode(boot::StayReason why)
{
    hal::startSystemClock(board::CrystalHz);
    Clock clock;
    // Static: its request buffer is larger than the main stack.
    static boot::LoaderProtocol protocol(loader, "D878UV2", dmm::Version);
    protocol.setStayReason(why);
    usbDevice.start();
    cm::enableInterrupts();
    std::uint32_t lastHeard = clock.nowMs();
    std::uint8_t input[64];
    for (;;) {
        hal::feedWatchdog();
        const std::uint32_t now = clock.nowMs();
        const std::size_t n = port.read(input);
        if (n != 0) {
            lastHeard = now;
            protocol.feed(std::span(input, n), now, [&clock](std::span<const std::uint8_t> b) { writeAll(b, clock); });
        }
        if (protocol.restart().wanted) {
            restart(clock, protocol.restart());
        }
        if (now - lastHeard > IdleRestartMs && somethingToStart()) {
            restart(clock, {});
        }
        hal::write(board::BlueLed, now % 1000 < 500);
    }
}

} // namespace

extern "C" void irq20Handler()
{
    usbd.handleInterrupt();
}

void hal::boardMain()
{
    for (Port p : board::StartPorts) {
        enablePortClock(p);
    }
    for (Pin pin : board::StartLowPins) {
        write(pin, false);
        configure(pin, PinMode::Output);
    }
    startWatchdog(LoaderWatchdogMs);

    // The app reads and clears the reset flags; they are left for it.
    const std::uint32_t flags = readResetFlags().raw;
    const bool powerOn = (flags & rcu::ResetByPower) != 0;
    const bool restarted = (flags & (rcu::ResetBySoftware | rcu::ResetByFreeWatchdog)) != 0;
    std::uint32_t argument = 0;
    boot::HandoverRequest request = boot::readHandover(board::loadHandover(handover), argument);
    board::storeHandover(handover, {});
    if (powerOn || !restarted) {
        request = boot::HandoverRequest::None;
    }

    boot::BootInputs in;
    in.request = request == boot::HandoverRequest::Stay ? boot::Request::Stay
        : request == boot::HandoverRequest::Rollback    ? boot::Request::Rollback
                                                        : boot::Request::None;
    in.swapCut = request == boot::HandoverRequest::SwapCut ? static_cast<std::uint16_t>(argument) : 0;
    in.forceKey = powerOn && forceKeysHeld();

    const boot::BootResult result = loader.boot(in);
    switch (result.outcome) {
    case boot::Outcome::StartImage:
        feedWatchdog();
        cm::startImageAt(layout::SlotABase);
    case boot::Outcome::Reset:
        cm::systemReset();
    case boot::Outcome::LoaderMode:
        break;
    }
    loaderMode(result.stay);
}
