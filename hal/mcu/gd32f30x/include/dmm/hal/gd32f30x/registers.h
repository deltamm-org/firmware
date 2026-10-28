// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// GD32F30x peripheral addresses and register offsets used by this HAL, from
// the GD32F30x User Manual.

#include <cstdint>

namespace dmm::hal::gd32f30x {

inline volatile std::uint32_t &reg(std::uintptr_t address)
{
    return *reinterpret_cast<volatile std::uint32_t *>(address);
}

namespace rcu {
inline constexpr std::uintptr_t Base = 0x40021000;
inline constexpr std::uintptr_t Ctl = Base + 0x00;
inline constexpr std::uintptr_t Cfg0 = Base + 0x04;
inline constexpr std::uintptr_t Apb2En = Base + 0x18;
inline constexpr std::uintptr_t Apb1En = Base + 0x1C;
inline constexpr std::uintptr_t ResetSource = Base + 0x24; // RCU_RSTSCK
inline constexpr std::uintptr_t AddCtl = Base + 0xC0;      // RCU_ADDCTL

// RCU_CTL
inline constexpr std::uint32_t Irc8mOn = 1u << 0;
inline constexpr std::uint32_t Irc8mStable = 1u << 1;
inline constexpr std::uint32_t HxtalOn = 1u << 16;
inline constexpr std::uint32_t HxtalStable = 1u << 17;
inline constexpr std::uint32_t HxtalBypass = 1u << 18;
inline constexpr std::uint32_t ClockMonitorOn = 1u << 19;
inline constexpr std::uint32_t PllOn = 1u << 24;
inline constexpr std::uint32_t PllStable = 1u << 25;

// RCU_CFG0
inline constexpr std::uint32_t SysClockMask = 3u << 0;
inline constexpr std::uint32_t SysClockStatusMask = 3u << 2;
inline constexpr std::uint32_t SysClockIrc8m = 0;
inline constexpr std::uint32_t SysClockPll = 2;
inline constexpr std::uint32_t Apb1Div2 = 4u << 8;
inline constexpr std::uint32_t PllFromHxtal = 1u << 16; // else IRC8M / 2
inline constexpr std::uint32_t UsbClockMask = (3u << 22) | (1u << 31);
inline constexpr std::uint32_t UsbClockPllDiv2p5 = 2u << 22;

/// PLL multiplier field: bits 21:18 hold bits 3:0, bit 27 bit 4, bit 30 bit 5
/// of (multiplier - 2).
constexpr std::uint32_t pllMultiplier(unsigned multiplier)
{
    const std::uint32_t m = multiplier - 2;
    return ((m & 0xFu) << 18) | (((m >> 4) & 1u) << 27) | (((m >> 5) & 1u) << 30);
}

inline constexpr std::uint32_t AdcPrescalerMask = (3u << 14) | (1u << 28);
inline constexpr std::uint32_t AdcApb2Div8 = 3u << 14;

// RCU_APB2EN
inline constexpr std::uint32_t AfioClock = 1u << 0;
inline constexpr std::uint32_t Adc0Clock = 1u << 9;
inline constexpr std::uint32_t Timer7Clock = 1u << 13;
constexpr std::uint32_t gpioClock(unsigned port)
{
    return 1u << (2 + port);
}

// RCU_APB1EN
inline constexpr std::uint32_t Timer2Clock = 1u << 1;
inline constexpr std::uint32_t Timer3Clock = 1u << 2;
inline constexpr std::uint32_t Timer4Clock = 1u << 3;
inline constexpr std::uint32_t Timer6Clock = 1u << 5;
inline constexpr std::uint32_t UsbdClock = 1u << 23;
inline constexpr std::uint32_t DacClock = 1u << 29;

// RCU_RSTSCK
inline constexpr std::uint32_t ClearResetFlags = 1u << 24;
inline constexpr std::uint32_t ResetByPin = 1u << 26;
inline constexpr std::uint32_t ResetByPower = 1u << 27;
inline constexpr std::uint32_t ResetBySoftware = 1u << 28;
inline constexpr std::uint32_t ResetByFreeWatchdog = 1u << 29;
inline constexpr std::uint32_t ResetByWindowWatchdog = 1u << 30;
inline constexpr std::uint32_t ResetByLowPower = 1u << 31;
} // namespace rcu

namespace usbd {
inline constexpr std::uintptr_t Base = 0x40005C00;
constexpr std::uintptr_t endpoint(unsigned n)
{
    return Base + 4 * n;
}
inline constexpr std::uintptr_t Ctl = Base + 0x40;
inline constexpr std::uintptr_t Intf = Base + 0x44;
inline constexpr std::uintptr_t Daddr = Base + 0x4C;
inline constexpr std::uintptr_t Baddr = Base + 0x50;
/// Packet memory: 512 bytes, each 16-bit word at a 32-bit-aligned address.
inline constexpr std::uintptr_t PacketMemory = 0x40006000;

// USBD_CTL
inline constexpr std::uint32_t ForceReset = 1u << 0;
inline constexpr std::uint32_t Close = 1u << 1;
inline constexpr std::uint32_t SetSuspend = 1u << 3;
inline constexpr std::uint32_t ResetIrq = 1u << 10;
inline constexpr std::uint32_t SuspendIrq = 1u << 11;
inline constexpr std::uint32_t WakeupIrq = 1u << 12;
inline constexpr std::uint32_t TransferIrq = 1u << 15;

// USBD_INTF
inline constexpr std::uint32_t EndpointMask = 0xF;
inline constexpr std::uint32_t DirectionOut = 1u << 4;
inline constexpr std::uint32_t ResetFlag = 1u << 10;
inline constexpr std::uint32_t SuspendFlag = 1u << 11;
inline constexpr std::uint32_t WakeupFlag = 1u << 12;
inline constexpr std::uint32_t TransferFlag = 1u << 15;

// USBD_DADDR
inline constexpr std::uint32_t Enable = 1u << 7;

// USBD_EPxCS. RX_ST and TX_ST clear when written 0; the DTG and STA bits
// toggle when written 1; type, kind and address are plain read-write.
inline constexpr std::uint32_t RxDone = 1u << 15;
inline constexpr std::uint32_t RxToggle = 1u << 14;
inline constexpr std::uint32_t RxStatusMask = 3u << 12;
inline constexpr std::uint32_t Setup = 1u << 11;
inline constexpr std::uint32_t TypeMask = 3u << 9;
inline constexpr std::uint32_t KindBit = 1u << 8;
inline constexpr std::uint32_t TxDone = 1u << 7;
inline constexpr std::uint32_t TxToggle = 1u << 6;
inline constexpr std::uint32_t TxStatusMask = 3u << 4;
inline constexpr std::uint32_t AddressMask = 0xF;
inline constexpr std::uint32_t PlainBits = TypeMask | KindBit | AddressMask;
inline constexpr std::uint32_t TypeBulk = 0u << 9;
inline constexpr std::uint32_t TypeControl = 1u << 9;
inline constexpr std::uint32_t TypeInterrupt = 3u << 9;
// Handshake states, for TX_STA (shift 4) and RX_STA (shift 12).
inline constexpr std::uint32_t Stall = 1;
inline constexpr std::uint32_t Nak = 2;
inline constexpr std::uint32_t Valid = 3;
} // namespace usbd

namespace afio {
inline constexpr std::uintptr_t Base = 0x40010000;
inline constexpr std::uintptr_t Pcf0 = Base + 0x04;
/// EXTI source selection: 4 lines per register, 4 bits each (port number).
constexpr std::uintptr_t extiSource(unsigned line)
{
    return Base + 0x08 + 4 * (line / 4);
}
inline constexpr std::uint32_t SwjMask = 7u << 24;
inline constexpr std::uint32_t SwjAllOff = 4u << 24;
inline constexpr std::uint32_t SwjSwdOnly = 2u << 24;
} // namespace afio

namespace exti {
inline constexpr std::uintptr_t Base = 0x40010400;
inline constexpr std::uintptr_t Inten = Base + 0x00;
inline constexpr std::uintptr_t Rten = Base + 0x08;
inline constexpr std::uintptr_t Ften = Base + 0x0C;
inline constexpr std::uintptr_t Pd = Base + 0x14;
} // namespace exti

namespace adc {
inline constexpr std::uintptr_t Adc0 = 0x40012400;
inline constexpr std::uintptr_t Stat = 0x00;
inline constexpr std::uintptr_t Ctl1 = 0x08;
inline constexpr std::uintptr_t Sampt0 = 0x0C; // channels 10..17
inline constexpr std::uintptr_t Sampt1 = 0x10; // channels 0..9
inline constexpr std::uintptr_t Rsq0 = 0x2C;
inline constexpr std::uintptr_t Rsq2 = 0x34;
inline constexpr std::uintptr_t Rdata = 0x4C;

inline constexpr std::uint32_t EndOfConversion = 1u << 1;
inline constexpr std::uint32_t On = 1u << 0;
inline constexpr std::uint32_t Calibrate = 1u << 2;
inline constexpr std::uint32_t ResetCalibration = 1u << 3;
inline constexpr std::uint32_t SoftwareTrigger = 7u << 17;
inline constexpr std::uint32_t TriggerEnable = 1u << 20;
inline constexpr std::uint32_t SoftwareStart = 1u << 22;
inline constexpr std::uint32_t LongestSample = 7; // 239.5 cycles
} // namespace adc

namespace dac {
inline constexpr std::uintptr_t Base = 0x40007400;
inline constexpr std::uintptr_t Ctl = Base + 0x00;
/// 12-bit right-aligned data registers of channels 0 and 1.
constexpr std::uintptr_t data12(unsigned channel)
{
    return Base + (channel == 0 ? 0x08 : 0x14);
}
/// DAC_CTL: channel n on.
constexpr std::uint32_t enable(unsigned channel)
{
    return 1u << (16 * channel);
}
/// DAC_CTL: channel n's output buffer bypassed.
constexpr std::uint32_t bufferOff(unsigned channel)
{
    return 1u << (16 * channel + 1);
}
} // namespace dac

namespace timer {
inline constexpr std::uintptr_t Timer2 = 0x40000400;
inline constexpr std::uintptr_t Timer3 = 0x40000800;
inline constexpr std::uintptr_t Timer4 = 0x40000C00;
inline constexpr std::uintptr_t Timer6 = 0x40001400;
inline constexpr std::uintptr_t Timer7 = 0x40013400;
inline constexpr std::uintptr_t Ctl0 = 0x00;
inline constexpr std::uintptr_t Dmainten = 0x0C;
inline constexpr std::uintptr_t Intf = 0x10;
inline constexpr std::uintptr_t Swevg = 0x14;
inline constexpr std::uintptr_t Chctl0 = 0x18; // channels 0 and 1
inline constexpr std::uintptr_t Chctl1 = 0x1C; // channels 2 and 3
inline constexpr std::uintptr_t Chctl2 = 0x20; // enables and polarities
inline constexpr std::uintptr_t Cnt = 0x24;
inline constexpr std::uintptr_t Psc = 0x28;
inline constexpr std::uintptr_t Car = 0x2C;
constexpr std::uintptr_t channelValue(unsigned ch)
{
    return 0x34 + 4 * ch;
}
inline constexpr std::uintptr_t Cchp = 0x44;

inline constexpr std::uint32_t CounterEnable = 1u << 0;
inline constexpr std::uint32_t AutoReloadShadow = 1u << 7;
inline constexpr std::uint32_t UpdateEvent = 1u << 0;
inline constexpr std::uint32_t UpdateInterrupt = 1u << 0; // DMAINTEN enable, INTF flag
inline constexpr std::uint32_t PrimaryOutputEnable = 1u << 15;
/// CHCTL0/1 output-compare bits of a channel, shifted for its half.
inline constexpr std::uint32_t PwmMode0 = 6u << 4;
inline constexpr std::uint32_t CompareShadow = 1u << 3;
/// CHCTL2 bits of channel n: enable, polarity, complementary enable, its polarity.
constexpr std::uint32_t channelEnable(unsigned n)
{
    return 1u << (4 * n);
}
constexpr std::uint32_t complementaryEnable(unsigned n)
{
    return 1u << (4 * n + 2);
}
constexpr std::uint32_t complementaryPolarity(unsigned n)
{
    return 1u << (4 * n + 3);
}
} // namespace timer

namespace fmc {
inline constexpr std::uintptr_t WaitState = 0x40022000; // FMC_WS
} // namespace fmc

namespace fwdgt {
inline constexpr std::uintptr_t Base = 0x40003000;
inline constexpr std::uintptr_t Ctl = Base + 0x00;
inline constexpr std::uintptr_t Psc = Base + 0x04;
inline constexpr std::uintptr_t Rld = Base + 0x08;
inline constexpr std::uintptr_t Stat = Base + 0x0C;
inline constexpr std::uint32_t KeyUnlock = 0x5555;
inline constexpr std::uint32_t KeyReload = 0xAAAA;
inline constexpr std::uint32_t KeyStart = 0xCCCC;
inline constexpr std::uint32_t PrescalerBusy = 1u << 0;
inline constexpr std::uint32_t ReloadBusy = 1u << 1;
} // namespace fwdgt

namespace gpio {
constexpr std::uintptr_t base(unsigned port)
{
    return 0x40010800 + 0x400 * port;
}
inline constexpr std::uintptr_t Ctl0 = 0x00; // pins 0-7, 4 bits each
inline constexpr std::uintptr_t Ctl1 = 0x04; // pins 8-15
inline constexpr std::uintptr_t Istat = 0x08;
inline constexpr std::uintptr_t Octl = 0x0C;
inline constexpr std::uintptr_t Bop = 0x10; // write 1: set pin
inline constexpr std::uintptr_t Bc = 0x14;  // write 1: clear pin
} // namespace gpio

} // namespace dmm::hal::gd32f30x
