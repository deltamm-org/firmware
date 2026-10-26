// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/radio/rda1846.h"

#include <span>

namespace dmm::drivers::radio {

namespace {

using Setting = Rda1846::Setting;
using WidthSetting = Rda1846::WidthSetting;
using BandWord = Rda1846::BandWord;

// The chip maker's start-up list for a 26 MHz reference (soft reset and
// the calibration run are done around it).
constexpr Setting MakerStartUp[] = {
    {0x04, 0x0FD0}, // 26 MHz reference
    {0x31, 0x0031},
    {0x33, 0x44A5}, // AGC
    {0x34, 0x2B87}, // receive digital gain
    {0x41, 0x470F}, // transmit voice digital gain
    {0x44, 0x0DFF}, // transmit gain, receive DAC gain and volume
    {0x47, 0x7FFF}, // soft mute
    {0x4F, 0x2C62},
    {0x53, 0x0094},
    {0x55, 0x0081},
    {0x56, 0x0B22}, // squelch detect time
    {0x57, 0x1C00}, // RSSI filter bypass
    {0x5A, 0x0EDB}, // squelch detect time
    {0x60, 0x101E}, // squelch noise threshold
    {0x63, 0x16AD}, // pre-emphasis bypass threshold
};

// Per channel width, from the same list. Page 1 holds the AGC table.
constexpr WidthSetting MakerWidth[] = {
    {0x15, 0x1100, 0x1F00, 0x1100},
    {0x32, 0x4495, 0x7564, 0x4495}, // AGC
    {0x3A, 0x40C3, 0x44C3, 0x40C3}, // transmit from the microphone; squelch detectors
    {0x3C, 0x0F1E, 0x172C, 0x0F1E}, // squelch peak detect
    {0x3F, 0x28D0, 0x29D2, 0x28D0}, // squelch RSSI threshold
    {0x48, 0x20BE, 0x2141, 0x20BE}, // squelch noise threshold
    {0x62, 0x0A10, 0x2346, 0x0A10},
    {0x65, 0x2494, 0x248A, 0x2494},
    {0x66, 0xEB2E, 0xFF2E, 0xEB2E}, // RSSI compensation
};

constexpr WidthSetting MakerWidthPage1[] = {
    {0x06, 0x0014, 0x0024, 0x0014},
    {0x07, 0x020C, 0x0214, 0x020C},
    {0x08, 0x0214, 0x0224, 0x0214},
    {0x09, 0x030C, 0x0314, 0x030C},
    {0x0A, 0x0314, 0x0324, 0x0314},
    {0x0B, 0x0324, 0x0344, 0x0324},
    {0x0C, 0x0344, 0x0384, 0x0344},
    {0x0D, 0x1344, 0x1384, 0x1344},
    {0x0E, 0x1B44, 0x1B84, 0x1B44},
    {0x0F, 0x3F44, 0x3F84, 0x3F44},
    {0x12, 0xE0EB, 0xE0EB, 0xE0EB},
};

// The band setting for frequencies near the 26 MHz reference's harmonics
// (136.5, 409.5 and 455 MHz).
constexpr BandWord MakerBandWords[] = {
    {135'500'000, 137'500'000, 0x8543},
    {408'500'000, 410'500'000, 0x8543},
    {454'000'000, 456'000'000, 0x8543},
};
constexpr std::uint16_t BandNormal = 0x8763;

constexpr Rda1846::Tuning Maker{
    .startUp = MakerStartUp,
    .afterCalibration = {},
    .width = MakerWidth,
    .widthPage1 = MakerWidthPage1,
    .ranges = {},
    .receive = {},
    .transmit = {},
    .bandWords = MakerBandWords,
    .normalBand = BandNormal,
};

constexpr std::uint8_t ControlRegister = 0x30;
constexpr std::uint8_t PageRegister = 0x7F;
constexpr std::uint8_t RssiRegister = 0x1B;
constexpr std::uint8_t BandRegister = 0x05;
constexpr std::uint8_t FrequencyHigh = 0x29;
constexpr std::uint8_t FrequencyLow = 0x2A;
constexpr std::uint8_t SquelchRegister = 0x49;
constexpr std::uint8_t NoiseSquelchRegister = 0x48;
constexpr std::uint8_t DeviationRegister = 0x59;
constexpr std::uint8_t Ctcss1Register = 0x4A;
constexpr std::uint8_t DcsHighRegister = 0x4B;
constexpr std::uint8_t DcsLowRegister = 0x4C;
constexpr std::uint8_t ToneModeRegister = 0x4E;
// The low five bits of this register switch the tone detectors on; the
// driver owns them, whatever a tuning table writes there.
constexpr std::uint8_t DetectRegister = 0x3A;
constexpr std::uint16_t DetectBits = 0x1F;
constexpr std::uint16_t DetectCtcss = 1u << 0;
constexpr std::uint16_t DetectDcs = 1u << 1;
constexpr std::uint16_t DetectDcsInverted = 1u << 2;

// Control register: reference 26 MHz, chip powered, and for wide channels
// both the analog and the DSP filters at 25 kHz.
constexpr std::uint16_t ControlNarrow = 0x4006;
constexpr std::uint16_t ControlWide = 0x7006;
constexpr std::uint16_t ReceiveOn = 1u << 5;
constexpr std::uint16_t TransmitOn = 1u << 6;
// The chip maker's calibration run after reset, in two steps.
constexpr std::uint16_t CalibrateA = 0x40A4;
constexpr std::uint16_t CalibrateB = 0x40A6;

// Tone mode: no tone, CTCSS (bits 10:9 = 11) or DCS (10, bit 7 set, bit 8
// for inverted, 23-bit words), no phase shift at the end.
constexpr std::uint16_t ToneModeOff = 0x2002;
constexpr std::uint16_t ToneModeCtcss = 0x2602;
constexpr std::uint16_t ToneModeDcs = 0x2482;
constexpr std::uint16_t ToneModeDcsInverted = 0x2582;
// DCS runs on the CTCSS generator at 134.4 Hz.
constexpr std::uint16_t DcsClockCentiHz = 13'440;
// Golay (23,12) generator for the DCS parity bits.
constexpr std::uint32_t GolayGenerator = 0xC75;

constexpr int RssiOffsetDb = 137;

} // namespace

const Rda1846::Tuning &Rda1846::makerTuning()
{
    return Maker;
}

Rda1846::Rda1846(hal::RegisterBus &bus, const Tuning &tuning)
    : m_bus(bus), m_tuning(&tuning)
{
}

void Rda1846::write(std::span<const Setting> settings, void (*sleepMs)(unsigned))
{
    for (const Setting &s : settings) {
        m_bus.write(s.reg, s.value);
        if (s.sleepMs != 0) {
            sleepMs(s.sleepMs);
        }
    }
}

void Rda1846::write(std::span<const WidthSetting> settings)
{
    for (const WidthSetting &s : settings) {
        std::uint16_t value = m_bandwidth == Bandwidth::Wide ? s.wide : m_bandwidth == Bandwidth::Digital ? s.digital
                                                                                                          : s.narrow;
        if (s.reg == DetectRegister) {
            value = static_cast<std::uint16_t>((value & ~DetectBits) | m_detect);
        }
        m_bus.write(s.reg, value);
    }
}

bool Rda1846::present()
{
    return m_bus.read(ChipIdRegister) == ChipId;
}

std::uint16_t Rda1846::setGpio(unsigned pin, GpioMode mode)
{
    if (pin > 7) {
        return m_bus.read(GpioRegister);
    }
    const unsigned shift = 2 * pin;
    std::uint16_t value = m_bus.read(GpioRegister);
    value = static_cast<std::uint16_t>((value & ~(3u << shift)) | (static_cast<unsigned>(mode) << shift));
    m_bus.write(GpioRegister, value);
    return value;
}

bool Rda1846::start(void (*sleepMs)(unsigned), std::uint16_t gpio)
{
    if (!present()) {
        return false;
    }
    m_bus.write(ControlRegister, 0x0001); // soft reset
    sleepMs(50);
    m_bus.write(ControlRegister, 0x0004); // power on
    m_bus.write(GpioRegister, gpio);
    write(m_tuning->startUp, sleepMs);
    m_bus.write(ControlRegister, CalibrateA);
    sleepMs(50);
    m_bus.write(ControlRegister, CalibrateB);
    sleepMs(100);
    m_bus.write(ControlRegister, ControlNarrow);
    sleepMs(10);
    write(m_tuning->afterCalibration, sleepMs);
    m_mode = Mode::Idle;
    setBandwidth(Bandwidth::Narrow);
    return true;
}

std::uint32_t Rda1846::frequencyWord(std::uint32_t hz)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(hz) * 16 / 1000);
}

void Rda1846::setFrequency(std::uint32_t hz)
{
    m_hz = hz;
    std::uint16_t band = m_tuning->normalBand;
    for (const BandWord &b : m_tuning->bandWords) {
        if (hz >= b.lowHz && hz <= b.highHz) {
            band = b.value;
            break;
        }
    }
    m_bus.write(BandRegister, band);
    const std::uint32_t word = frequencyWord(hz);
    m_bus.write(FrequencyHigh, static_cast<std::uint16_t>(word >> 16));
    m_bus.write(FrequencyLow, static_cast<std::uint16_t>(word));
}

void Rda1846::setBandwidth(Bandwidth bandwidth)
{
    m_bandwidth = bandwidth;
    write(m_tuning->width);
    m_bus.write(PageRegister, 1);
    write(m_tuning->widthPage1);
    m_bus.write(PageRegister, 0);
    for (const RangeSetting &r : m_tuning->ranges) {
        if (m_hz >= r.lowHz && m_hz <= r.highHz) {
            write(std::span(&r.setting, 1));
        }
    }
}

void Rda1846::setMode(Mode mode)
{
    const std::uint16_t idle = m_bandwidth == Bandwidth::Wide ? ControlWide : ControlNarrow;
    m_bus.write(ControlRegister, idle);
    m_mode = mode;
    if (mode == Mode::Idle) {
        return;
    }
    const bool transmit = mode == Mode::Transmit;
    write(transmit ? m_tuning->transmit : m_tuning->receive);
    m_bus.write(ControlRegister, static_cast<std::uint16_t>(idle | (transmit ? TransmitOn : ReceiveOn)));
}

std::uint16_t Rda1846::squelchWord(int openDbm, int closeDbm)
{
    auto field = [](int dbm) {
        const int v = dbm + RssiOffsetDb;
        return static_cast<std::uint16_t>(v < 0 ? 0 : v > 0x7F ? 0x7F
                                                               : v);
    };
    return static_cast<std::uint16_t>(field(openDbm) << 7 | field(closeDbm));
}

void Rda1846::setSquelch(int openDbm, int closeDbm)
{
    m_bus.write(SquelchRegister, squelchWord(openDbm, closeDbm));
}

void Rda1846::setNoiseSquelch(int openDb, int closeDb)
{
    auto field = [](int db) { return static_cast<std::uint16_t>(db < 0 ? 0 : db > 0x7F ? 0x7F
                                                                                       : db); };
    m_bus.write(NoiseSquelchRegister, static_cast<std::uint16_t>(field(openDb) << 7 | field(closeDb)));
}

int Rda1846::noiseDb()
{
    return static_cast<int>(m_bus.read(RssiRegister) & 0xFF);
}

int Rda1846::rssiDbm()
{
    return static_cast<int>(m_bus.read(RssiRegister) >> 8) - RssiOffsetDb;
}

std::uint32_t Rda1846::dcsWord(std::uint16_t code)
{
    const std::uint32_t data = 0x800u | (code & 0x1FFu);
    std::uint32_t remainder = data << 11;
    for (int bit = 22; bit >= 11; --bit) {
        if ((remainder & (1u << bit)) != 0) {
            remainder ^= GolayGenerator << (bit - 11);
        }
    }
    return (remainder & 0x7FFu) << 12 | data;
}

void Rda1846::setTone(const SubTone &tone)
{
    switch (tone.kind) {
    case SubTone::Kind::None:
        m_bus.write(ToneModeRegister, ToneModeOff);
        break;
    case SubTone::Kind::Ctcss:
        m_bus.write(Ctcss1Register, tone.value);
        m_bus.write(ToneModeRegister, ToneModeCtcss);
        break;
    case SubTone::Kind::Dcs: {
        const std::uint32_t word = dcsWord(tone.value);
        m_bus.write(Ctcss1Register, DcsClockCentiHz);
        m_bus.write(DcsHighRegister, static_cast<std::uint16_t>(word >> 16));
        m_bus.write(DcsLowRegister, static_cast<std::uint16_t>(word & 0xFFFF));
        m_bus.write(ToneModeRegister, tone.inverted ? ToneModeDcsInverted : ToneModeDcs);
        break;
    }
    }
}

void Rda1846::setTransmitTone(const SubTone &tone)
{
    setTone(tone);
}

void Rda1846::setReceiveTone(const SubTone &tone)
{
    setTone(tone);
    switch (tone.kind) {
    case SubTone::Kind::None:
        m_detect = 0;
        m_toneFlag = 0;
        break;
    case SubTone::Kind::Ctcss:
        m_detect = DetectCtcss;
        m_toneFlag = CtcssMatch;
        break;
    case SubTone::Kind::Dcs:
        m_detect = tone.inverted ? DetectDcsInverted : DetectDcs;
        m_toneFlag = tone.inverted ? DcsInvertedMatch : DcsMatch;
        break;
    }
    const std::uint16_t detect = m_bus.read(DetectRegister);
    m_bus.write(DetectRegister, static_cast<std::uint16_t>((detect & ~DetectBits) | m_detect));
}

void Rda1846::setDeviation(std::uint16_t voice, std::uint8_t tone)
{
    m_bus.write(DeviationRegister, static_cast<std::uint16_t>((voice & 0x3FF) << 6 | (tone & 0x3F)));
}

} // namespace dmm::drivers::radio
