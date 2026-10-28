// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

// AnyTone D878UVII: pins, start-up order and the service implementations.

#include "board_adc.h"
#include "board_audio.h"
#include "board_baseband.h"
#include "board_display.h"
#include "board_fm_tuning.h"
#include "board_rf.h"
#include "dmm/board/board_battery.h"
#include "dmm/board/board_input.h"
#include "panel_bus.h"
#include "pins.h"

#include "dmm/app/app.h"
#include "dmm/baseband_boot_blob.h"
#include "dmm/drivers/baseband/sct3258.h"
#include "dmm/drivers/radio/rda1846.h"
#include "dmm/drivers/storage/spi_nand.h"
#include "dmm/drivers/usb/cdc_acm.h"
#include "dmm/drivers/usb/descriptors.h"
#include "dmm/drivers/usb/device.h"
#include "dmm/hal/cortex_m/core.h"
#include "dmm/hal/cortex_m/fault.h"
#include "dmm/hal/gd32f30x/bitbang_spi_nand_bus.h"
#include "dmm/hal/gd32f30x/clock.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/reset.h"
#include "dmm/hal/gd32f30x/startup.h"
#include "dmm/hal/gd32f30x/usbd.h"
#include "dmm/hal/gd32f30x/watchdog.h"
#include "dmm/os/os.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_storage.h"
#include "dmm/services/log.h"
#include "dmm/services/rda1846_transceiver.h"
#include "dmm/services/sct3258_baseband.h"
#include "dmm/services/usb_channel.h"
#include "dmm/services/usb_log.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>

namespace hal = dmm::hal::gd32f30x;
using hal::Pin;
using hal::PinMode;
using hal::Port;
using namespace board;

namespace board {

hal::ParallelLcdBus &lcdBus()
{
    static hal::ParallelLcdBus bus{LcdPins};
    return bus;
}

dmm::os::Mutex &panelPinsLock()
{
    static dmm::os::Mutex lock;
    return lock;
}

SharedAdc &adc()
{
    static SharedAdc shared;
    return shared;
}

} // namespace board

namespace {

void outputLow(Pin pin)
{
    hal::write(pin, false);
    hal::configure(pin, PinMode::Output);
}

class BoardLeds final : public dmm::services::Leds
{
public:
    void set(Led led, bool on) override
    {
        hal::write(led == Led::Red ? RedLed : GreenLed, on);
    }
};

namespace cm = dmm::hal::cortex_m;

const char *resetCauseName(hal::ResetCause cause)
{
    switch (cause) {
    case hal::ResetCause::PowerOn:
        return "power-on";
    case hal::ResetCause::ResetPin:
        return "reset pin";
    case hal::ResetCause::Software:
        return "software";
    case hal::ResetCause::Watchdog:
        return "watchdog";
    case hal::ResetCause::WindowWatchdog:
        return "window watchdog";
    case hal::ResetCause::LowPower:
        return "low-power entry";
    case hal::ResetCause::Unknown:
        break;
    }
    return "unknown";
}

const char *crashKindName(cm::CrashKind kind)
{
    switch (kind) {
    case cm::CrashKind::HardFault:
        return "hard fault";
    case cm::CrashKind::UnexpectedInterrupt:
        return "unexpected interrupt";
    case cm::CrashKind::StackOverflow:
        return "stack overflow";
    case cm::CrashKind::KernelCheck:
        return "kernel check failed";
    case cm::CrashKind::TestFault:
        return "test fault";
    case cm::CrashKind::None:
        break;
    }
    return "unknown";
}

// Read once at start, before anything can clear them.
dmm::services::ResetReport takeResetReport()
{
    const hal::ResetFlags flags = hal::takeResetFlags();
    dmm::services::ResetReport report;
    report.cause = resetCauseName(flags.cause);
    report.flags = flags.raw;
    cm::CrashRecord record;
    if (cm::takeCrashRecord(record)) {
        report.crashed = true;
        dmm::services::CrashReport &c = report.crash;
        c.kind = crashKindName(record.kind);
        c.detail = record.detail;
        c.pc = record.pc;
        c.lr = record.lr;
        c.psr = record.psr;
        c.faultStatus = record.cfsr;
        c.hardFaultStatus = record.hfsr;
        c.faultAddress = record.faultAddress;
        for (std::size_t i = 0; i < sizeof c.task; ++i) {
            c.task[i] = record.task[i];
        }
    }
    return report;
}

void onKernelFatal(const char *what, const char *task)
{
    const std::string_view w(what);
    cm::crash(w == "stack overflow" ? cm::CrashKind::StackOverflow : cm::CrashKind::KernelCheck, 0, task);
}

class BoardSystem final : public dmm::services::SystemControl
{
public:
    explicit BoardSystem(const dmm::services::ResetReport &reset)
        : m_reset(reset)
    {
    }

    bool powerSwitchOff() override
    {
        return hal::read(PowerKnobOff);
    }
    void holdPower() override
    {
        hal::write(PowerHold, true);
    }
    void releasePower() override
    {
        hal::write(PowerHold, false);
    }
    void feedWatchdog() override
    {
        hal::feedWatchdog();
    }
    void restart() override
    {
        cm::systemReset();
    }
    dmm::services::ResetReport lastReset() override
    {
        return m_reset;
    }
    void testFault() override
    {
        cm::triggerTestFault();
    }

private:
    dmm::services::ResetReport m_reset;
};

namespace usb = dmm::drivers::usb;

// USB identity: the pid.codes test IDs while DeltaMM is in development.
// Two serial ports: the data port (DeltaMM's PC protocol), then the log.
constexpr auto DeviceDescriptor = usb::compositeDeviceDescriptor({0x1209, 0x0001, 0x0001, 1, 2, 0});
constexpr auto DataPort = usb::cdcAcmDescriptors(0, 0x83, 0x01, 0x81, 4);
constexpr auto LogPort = usb::cdcAcmDescriptors(2, 0x84, 0x02, 0x82, 3);
constexpr auto ConfigurationDescriptor =
    usb::join(usb::configurationHeader(9 + 2 * usb::CdcAcmDescriptorBytes, 4, 50), DataPort, LogPort);
constexpr auto Language = usb::languageDescriptor();
constexpr auto Manufacturer = usb::stringDescriptor("DeltaMM");
constexpr auto Product = usb::stringDescriptor("DeltaMM AnyTone D878UVII");
constexpr auto LogPortName = usb::stringDescriptor("DeltaMM log");
constexpr auto DataPortName = usb::stringDescriptor("DeltaMM data");
constexpr std::span<const std::uint8_t> Strings[] = {Language, Manufacturer, Product, LogPortName, DataPortName};

hal::Usbd usbd{UsbPullUp, UsbIrqPriority};
usb::CdcAcm dataPort{0, 0x83, 0x01, 0x81};
usb::CdcAcm logPort{2, 0x84, 0x02, 0x82};
usb::Function *const usbFunctions[] = {&dataPort, &logPort};
usb::Device usbDevice{usbd, usb::Descriptors{DeviceDescriptor, ConfigurationDescriptor, Strings}, usbFunctions};

void delayUs(unsigned us)
{
    cm::busyWaitCycles(us * (hal::SystemClockHz / 1'000'000));
}

// The panel variant strap on the reset pin, read twice 50 ms apart before
// that pin becomes an output. Unstable reads count as 0.
bool readPanelStrap()
{
    hal::configure(PanelStrap, PinMode::InputPullDown);
    cm::busyWaitCycles(hal::SystemClockHz / 500); // 2 ms
    const bool first = hal::read(PanelStrap);
    cm::busyWaitCycles(hal::SystemClockHz / 20); // 50 ms
    return first && hal::read(PanelStrap);
}

} // namespace

extern "C" void irq20Handler()
{
    usbd.handleInterrupt();
}

extern "C" void irq6Handler()
{
    BoardRf::onSlotEdge();
}

extern "C" void irq55Handler()
{
    BoardRf::onRampStep();
}

extern "C" void irq23Handler()
{
    BoardInput::onEncoderEdge();
}

extern "C" void irq50Handler()
{
    BoardAudio::onToneInterrupt();
}

void hal::boardMain()
{
    static const dmm::services::ResetReport resetReport = takeResetReport();
    cm::setTaskNameSource(dmm::os::currentTaskName);
    dmm::os::setFatalHandler(onKernelFatal);
    const ClockSource clock = startSystemClock(CrystalHz);
    cm::enableCycleCounter();

    for (Port port : {Port::A, Port::B, Port::C, Port::D, Port::E}) {
        enablePortClock(port);
    }
    for (Pin pin : {TransmitEnable, PowerAmplifierEnable, SpeakerAmplifier, ReceiveAudioRoute, BluetoothPower, PowerHold, RedLed, GreenLed, ThirdLed, UsbPullUp}) {
        outputLow(pin);
    }
    static BoardBaseband basebandBoard;
    basebandBoard.configurePins();
    configure(PowerKnobOff, PinMode::InputPullUp);
#ifdef DMM_KEEP_SWD
    releaseJtagPins(); // the data flash reads on a JTAG pin
#else
    releaseDebugPins(); // PF2 is on the debug clock pin
#endif
    static dmm::hal::gd32f30x::ThreeWireBus fmBus{FmTransceiverBus};
    fmBus.configurePins();

    const bool panelStrap = readPanelStrap();
    lcdBus().configurePins();
    static BitBangSpiNandBus flashBus{FlashPins};
    flashBus.configurePins();
    static dmm::drivers::storage::SpiNand flash{flashBus, delayUs};
    static dmm::services::D878Uv2Storage storage{flash};
    storage.start();
    static dmm::services::D878Uv2Calibration calibration;
    static dmm::services::D878Uv2Codeplug codeplug{storage};

    adc().start();
    static BoardInput input;
    input.configure();
    static BoardBattery battery;
    battery.configure();
    static BoardAudio audio;
    audio.configure();
    static BoardRf rf;
    rf.configure();
    static dmm::drivers::baseband::Sct3258 basebandChip{basebandBoard.bus(), BoardBaseband::pollDelayUs};
    // Called from the radio task; empty until the system task has read the
    // calibration.
    static dmm::services::Sct3258Baseband baseband{basebandChip, basebandBoard, dmm::baseband::bootBlob(), basebandAudio(),
        []() -> std::optional<dmm::services::Sct3258Baseband::Trims> {
            const auto gains = calibration.basebandAnalogGains();
            const auto codec = calibration.codecTrims();
            if (!gains || !codec) {
                return std::nullopt;
            }
            return dmm::services::Sct3258Baseband::Trims{*gains, {codec->leftOut, codec->rightOut, codec->rightInput}};
        },
        [](std::uint32_t hz) -> std::optional<dmm::services::Sct3258Baseband::DigitalTrims> {
            const auto offset = calibration.basebandDcOffset(hz);
            const auto gain = calibration.basebandDemodGain();
            const auto modulation = calibration.basebandModGain(hz);
            if (!offset || !gain || !modulation) {
                return std::nullopt;
            }
            return dmm::services::Sct3258Baseband::DigitalTrims{*offset, *gain, *modulation,
                calibration.basebandSpeakerGain(), calibration.basebandMicGain(), calibration.basebandSlotTiming()};
        }};
    static dmm::drivers::radio::Rda1846 fmChip{fmBus};
    // Called from the radio task; empty until the system task has read the
    // calibration.
    using Fm = dmm::services::Rda1846Transceiver;
    static const Fm::TrimsSource fmTrims{
        [](std::uint32_t hz, int squelchLevel) -> std::optional<Fm::ReceiveTrims> {
            const auto trim = calibration.referenceTrim();
            const auto tune = calibration.rxTune(hz);
            const auto threshold = calibration.squelchThreshold(hz, squelchLevel);
            if (!trim || !tune || !threshold) {
                return std::nullopt;
            }
            return Fm::ReceiveTrims{*trim, *tune, -*threshold};
        },
        [](std::uint32_t hz, dmm::services::PowerLevel level) -> std::optional<Fm::TransmitTrims> {
            const auto trim = calibration.referenceTrim();
            const auto power = calibration.txPower(hz, level);
            const auto deviation = calibration.deviation();
            if (!trim || !power || !deviation) {
                return std::nullopt;
            }
            return Fm::TransmitTrims{*trim, *power, deviation->voice, deviation->ctcss, deviation->dcs};
        }};
    static Fm fm{fmChip, rf, FmPins, [](unsigned ms) { dmm::os::sleepMs(ms); }, fmTrims,
        []() -> const dmm::drivers::radio::Rda1846::Tuning & { return fmTuning(&storage); }};

    startWatchdog(WatchdogTimeoutMs);

    static BoardLeds leds;
    static BoardSystem system{resetReport};
    static dmm::services::UsbLog logSink{logPort};
    static dmm::services::UsbChannel dataChannel{dataPort};
    static BoardDisplay display{panelStrap};
    usbDevice.start();
    static dmm::app::Platform platform{
        .leds = leds,
        .logSink = logSink,
        .system = system,
        .fm = fm,
        .boardName = "anytone_d878uv2",
        .bootReport = clock == ClockSource::Crystal ? "clock 120 MHz from crystal"
                                                    : "crystal did not start, clock 120 MHz from internal RC",
        .display = &display,
        .input = &input,
        .battery = &battery,
        .storage = &storage,
        .dataChannel = &dataChannel,
        .baseband = &baseband,
        .audio = &audio,
        .calibration = &calibration,
        .codeplug = &codeplug,
    };
    dmm::app::start(platform);
    dmm::os::runScheduler();
    for (;;) {
    }
}
