// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// AnyTone D878UVII pin assignments and board constants.

#include "dmm/hal/gd32f30x/bitbang_spi_nand_bus.h"
#include "dmm/hal/gd32f30x/gpio.h"
#include "dmm/hal/gd32f30x/parallel_host_port.h"
#include "dmm/hal/gd32f30x/parallel_lcd_bus.h"
#include "dmm/hal/gd32f30x/three_wire_bus.h"
#include "dmm/services/rda1846_transceiver.h"

#include <cstdint>

namespace board {

using dmm::hal::gd32f30x::Pin;
using dmm::hal::gd32f30x::Port;

inline constexpr std::uint32_t CrystalHz = 12'000'000;
inline constexpr std::uint32_t WatchdogTimeoutMs = 2500;

// Front-panel LEDs, high = on.
inline constexpr Pin RedLed{Port::E, 5};
inline constexpr Pin GreenLed{Port::E, 3};
inline constexpr Pin ThirdLed{Port::E, 4};

// High keeps the radio powered after the power knob is turned off.
inline constexpr Pin PowerHold{Port::C, 3};
// High while the power knob is in its off position.
inline constexpr Pin PowerKnobOff{Port::A, 0};

// High connects the USB D+ pull-up, so the host sees the radio.
inline constexpr Pin UsbPullUp{Port::E, 2};
inline constexpr unsigned UsbIrqPriority = 6;

// Display: ST7735 on an 8-bit parallel bus. The data lines PE7-PE14 are
// also the keypad rows. PB11 is read once as a panel variant strap, then
// drives the panel reset.
inline constexpr dmm::hal::gd32f30x::ParallelLcdBus::Pins LcdPins{
    Port::E, 7, {Port::B, 10}, {Port::B, 12}, {Port::B, 13}, {Port::B, 11}};
inline constexpr Pin PanelStrap{Port::B, 11};

// Backlight (display and keypad): TIMER7 channel 1 complementary output,
// lit while high, 7.3 kHz. High time per level 0 (off) to 5. The two panel
// variants differ: with strap 1 the light only starts at about 60 % duty,
// so its levels sit in the upper range.
inline constexpr Pin Backlight{Port::B, 0};
inline constexpr unsigned BacklightChannel = 1;
inline constexpr std::uint16_t BacklightPrescaler = 3;
inline constexpr std::uint16_t BacklightPeriod = 4095;
inline constexpr std::uint16_t BacklightDutyStrap0[] = {0, 40, 120, 400, 1300, 4096};
inline constexpr std::uint16_t BacklightDutyStrap1[] = {0, 2600, 2950, 3300, 3700, 4096};

// Keypad: 8 rows (PE7-PE14, driven low one at a time) by 3 columns with
// pull-ups. PF3 pulls the third column low on its own, so it is read with
// every row high.
inline constexpr Port KeypadRowPort = Port::E;
inline constexpr std::uint8_t KeypadFirstRow = 7;
inline constexpr Pin KeypadColumns[] = {{Port::C, 5}, {Port::B, 1}, {Port::B, 2}};
inline constexpr Pin Pf3Column = KeypadColumns[2];

// Side keys, low = pressed. PF2 shares its pin with the debug port's clock.
inline constexpr Pin Ptt{Port::A, 10};
inline constexpr Pin Pf1{Port::E, 1};
inline constexpr Pin Pf2{Port::A, 14};

// Channel knob: quadrature on PC8 (A, interrupt on both edges) and PC9 (B).
inline constexpr Pin EncoderA{Port::C, 8};
inline constexpr Pin EncoderB{Port::C, 9};
inline constexpr unsigned EncoderIrqPriority = 7;
/// Raw counts per click: each click is one edge of A.
inline constexpr int EncoderCountsPerDetent = 1;

// Volume knob: a potentiometer on PC2, ADC channel 12.
inline constexpr Pin VolumeKnob{Port::C, 2};
inline constexpr unsigned VolumeKnobChannel = 12;

// Data flash: SPI NAND, chip select PB8, clock PB14, data PB15 to the chip
// and PA15 from it (both inputs during dual reads).
inline constexpr dmm::hal::gd32f30x::BitBangSpiNandBus::Pins FlashPins{
    {Port::B, 8}, {Port::B, 14}, {Port::B, 15}, {Port::A, 15}};

// Battery: a 4:1 divider on PC1, ADC channel 11. Full scale (4095) is
// 13.2 V before the radio's calibration offset.
inline constexpr Pin BatterySense{Port::C, 1};
inline constexpr unsigned BatteryChannel = 11;
inline constexpr int BatteryFullScaleMv = 13200;

// Held low from the first instructions on: the transmit path and power
// amplifier enables, the speaker amplifier, its receive-audio switch and
// the Bluetooth module's power.
inline constexpr Pin TransmitEnable{Port::C, 4};
inline constexpr Pin PowerAmplifierEnable{Port::E, 6};
inline constexpr Pin SpeakerAmplifier{Port::A, 7};  // high = on
inline constexpr Pin ReceiveAudioRoute{Port::E, 0}; // high = codec output to the amplifier
inline constexpr Pin BluetoothPower{Port::A, 1};

// RF controls. PA4 (DAC0) sets the power amplifier's output; PA5 (DAC1)
// trims the reference oscillator; PA6 (TIMER2 channel 0) tunes the receive
// front-end filter, high for value / 4096 of each 29 kHz cycle.
inline constexpr Pin PowerControl{Port::A, 4};
inline constexpr unsigned PowerControlChannel = 0;
inline constexpr Pin ReferenceTrim{Port::A, 5};
inline constexpr unsigned ReferenceTrimChannel = 1;
inline constexpr Pin ReceiveTune{Port::A, 6};
inline constexpr unsigned ReceiveTuneChannel = 0;
inline constexpr std::uint16_t ReceiveTunePeriod = 0xFFF;

// Beeps: PWM on PB9 (TIMER3 channel 3), mixed in before the amplifier. The
// amplifier needs this long after switching on before a beep sounds clean.
inline constexpr Pin ToneOutput{Port::B, 9};
inline constexpr unsigned AmplifierSettleMs = 150;
inline constexpr unsigned ToneIrqPriority = 2;
// Beep level 15 gives this PWM duty peak (of 255).
inline constexpr unsigned LoudestTonePeak = 90;

// DMR baseband (SCT3258): an 8-bit host port on PD8-PD15 with chip select
// PD2, write PD3 and read PD4; PD5 is low while the chip has data. PD6 is
// its reset (low = reset). PD1 and PD7 (wake and restart inputs) and PC7
// stay low. PC12 is a mode strap: low through reset selects this kind of
// host port, then it is released. PA8, then PC6, enable its supplies or
// clock before the reset pulse.
inline constexpr dmm::hal::gd32f30x::ParallelHostPort::Pins BasebandPort{
    Port::D, 8, {Port::D, 2}, {Port::D, 3}, {Port::D, 4}, {Port::D, 5}};
inline constexpr Pin BasebandReset{Port::D, 6};
inline constexpr Pin BasebandWake{Port::D, 1};
inline constexpr Pin BasebandRestart{Port::D, 7};
inline constexpr Pin BasebandSleep{Port::C, 7};
inline constexpr Pin BasebandModeStrap{Port::C, 12};
inline constexpr Pin BasebandEnableFirst{Port::A, 8};
inline constexpr Pin BasebandEnableSecond{Port::C, 6};
// Reset timing: off time before a restart, enables to reset high, high
// time, low time (milliseconds).
inline constexpr unsigned BasebandOffMs = 200;
inline constexpr unsigned BasebandSettleMs = 100;
inline constexpr unsigned BasebandResetHighMs = 100;
inline constexpr unsigned BasebandResetLowMs = 500;

// FM transceiver (RDA1846): three-wire link, enable PB5, clock PB3, data
// PB4 (PB3 and PB4 are debug pins, free once the JTAG part of the debug
// port is off). Its GPIO pins switch the RF board: GPIO3 mutes the
// microphone while high; GPIO0 and GPIO4 turn on the receive front end
// above and below 300 MHz; GPIO5 selects the transmit path above 300 MHz
// and GPIO7 the transmit filter for 165-300 MHz; GPIO2 powers the GPS.
// The receive front-end tuning is a 12-bit PWM value.
inline constexpr dmm::hal::gd32f30x::ThreeWireBus::Pins FmTransceiverBus{{Port::B, 5}, {Port::B, 3}, {Port::B, 4}};
inline constexpr dmm::services::Rda1846Transceiver::Pins FmPins{3, 0, 4, 5, 7, 2, 300'000'000, 165'000'000, 0xFFF};

} // namespace board
