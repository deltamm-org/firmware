// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/register_bus.h"

#include <cstdint>
#include <span>

namespace dmm::drivers::radio {

/// The RDA1846 (AT1846S) FM transceiver with a 26 MHz reference. Register
/// meanings from the chip maker's programming guide and register table.
/// The register values come from a Tuning: the chip maker's generic one,
/// or one the board supplies.
class Rda1846
{
public:
    static constexpr std::uint8_t ChipIdRegister = 0x00;
    static constexpr std::uint16_t ChipId = 0x1846;
    static constexpr std::uint8_t GpioRegister = 0x1F;
    static constexpr std::uint8_t FlagsRegister = 0x1C;
    static constexpr std::uint8_t QualityRegister = 0x21;

    // Bits in the flags register.
    static constexpr std::uint16_t SquelchOpen = 1u << 0;
    static constexpr std::uint16_t DcsInvertedMatch = 1u << 6;
    static constexpr std::uint16_t DcsMatch = 1u << 7;
    static constexpr std::uint16_t CtcssMatch = 1u << 9;

    /// A sub-audible tone: CTCSS in 0.01 Hz units, or a DCS code (the nine
    /// bits of its three octal digits), normal or inverted.
    struct SubTone
    {
        enum class Kind : std::uint8_t
        {
            None,
            Ctcss,
            Dcs,
        };
        Kind kind = Kind::None;
        std::uint16_t value = 0;
        bool inverted = false;
    };

    enum class Bandwidth : std::uint8_t
    {
        Narrow, // 12.5 kHz channels
        Wide,   // 25 kHz channels
    };

    enum class Mode : std::uint8_t
    {
        Idle,
        Receive,
        Transmit,
    };

    /// What a GPIO pin does: two bits per pin in the GPIO register.
    enum class GpioMode : std::uint8_t
    {
        HighImpedance = 0,
        Function = 1, // the pin's built-in signal
        Low = 2,
        High = 3,
    };

    /// One register write, then a wait of @p sleepMs.
    struct Setting
    {
        std::uint8_t reg;
        std::uint16_t value;
        std::uint8_t sleepMs = 0;
    };
    /// One register with a value per channel width.
    struct WidthSetting
    {
        std::uint8_t reg;
        std::uint16_t narrow;
        std::uint16_t wide;
    };
    /// A width setting used only from lowHz to highHz (both included).
    struct RangeSetting
    {
        std::uint32_t lowHz;
        std::uint32_t highHz;
        WidthSetting setting;
    };
    /// The band register's value from lowHz to highHz (both included).
    struct BandWord
    {
        std::uint32_t lowHz;
        std::uint32_t highHz;
        std::uint16_t value;
    };

    /// Every register value the driver writes, apart from the ones its
    /// calls ask for (frequency, squelch, tones, mode).
    struct Tuning
    {
        /// After the reset and power-on, before the calibration run.
        std::span<const Setting> startUp;
        /// After the calibration run.
        std::span<const Setting> afterCalibration;
        /// Per channel width: page 0, then page 1 (the AGC table).
        std::span<const WidthSetting> width;
        std::span<const WidthSetting> widthPage1;
        /// Written after the width settings when the frequency is in range.
        std::span<const RangeSetting> ranges;
        /// Written on every switch to receive or to transmit, before the
        /// chip turns on.
        std::span<const WidthSetting> receive;
        std::span<const WidthSetting> transmit;
        /// The band register: the first entry whose range holds the
        /// frequency, else normalBand. Written on every tuning.
        std::span<const BandWord> bandWords;
        std::uint16_t normalBand;
    };

    /// The chip maker's start-up and per-width values for a 26 MHz
    /// reference, and its band setting near the reference's harmonics.
    static const Tuning &makerTuning();

    explicit Rda1846(hal::RegisterBus &bus, const Tuning &tuning = makerTuning());

    /// Uses @p tuning from the next start(), tuning and mode change.
    void setTuning(const Tuning &tuning) { m_tuning = &tuning; }

    /// True when the chip answers with its ID.
    bool present();

    /// Resets the chip and loads its start-up settings, narrow and idle.
    /// The reset clears the GPIO register; @p gpio is written back at once,
    /// before the slow calibration steps. @p sleepMs waits; about 210 ms in
    /// all, plus the tuning's own waits. False when the chip does not
    /// answer.
    bool start(void (*sleepMs)(unsigned), std::uint16_t gpio);

    /// Tunes to @p hz. Change it only while idle.
    void setFrequency(std::uint32_t hz);
    /// Filters and gains for the channel width, and those for the
    /// frequency, so after setFrequency(). Change it only while idle.
    void setBandwidth(Bandwidth bandwidth);
    /// Idle, receive or transmit, always passing through idle, with the
    /// drive and gain settings for that direction.
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    /// Squelch opens above @p openDbm and closes below @p closeDbm.
    void setSquelch(int openDbm, int closeDbm);
    /// The squelch's noise condition: it opens when the received noise
    /// falls below @p openDb and closes when it rises above @p closeDb.
    void setNoiseSquelch(int openDb, int closeDb);
    /// The flags register: squelch, tone matches.
    std::uint16_t flags() { return m_bus.read(FlagsRegister); }
    /// In @p flags, the receive tone set with setReceiveTone() is heard.
    bool receiveToneHeard(std::uint16_t flags) const { return (flags & m_toneFlag) != 0; }
    /// Received signal strength in dBm.
    int rssiDbm();
    /// Received noise in dB.
    int noiseDb();
    /// A receive quality figure, 0..15, lower for a strong clean signal
    /// (low four bits of register 0x21, not in the programming guide).
    int signalQuality() { return m_bus.read(QualityRegister) & 0xF; }

    /// The tone sent while transmitting.
    void setTransmitTone(const SubTone &tone);
    /// The tone the receiver listens for, with only its own detector on;
    /// Kind::None turns detection off. Kept across width changes.
    void setReceiveTone(const SubTone &tone);
    /// Transmit deviation: @p voice for voice and tone together (10 bits),
    /// @p tone for the tone alone (6 bits). Larger is wider.
    void setDeviation(std::uint16_t voice, std::uint8_t tone);

    std::uint16_t readRegister(std::uint8_t address) { return m_bus.read(address); }
    void writeRegister(std::uint8_t address, std::uint16_t value) { m_bus.write(address, value); }

    /// The frequency in the chip's 1/16 kHz units.
    static std::uint32_t frequencyWord(std::uint32_t hz);
    /// The squelch register for open and close levels in dBm.
    static std::uint16_t squelchWord(int openDbm, int closeDbm);
    /// The 23-bit DCS word for a nine-bit @p code, as the chip takes it:
    /// eleven Golay (23,12) parity bits, then 100, then the code.
    static std::uint32_t dcsWord(std::uint16_t code);

    /// Changes one GPIO pin (0..7), keeping the others as the chip has
    /// them. Returns the register value written.
    std::uint16_t setGpio(unsigned pin, GpioMode mode);

private:
    void write(std::span<const Setting> settings, void (*sleepMs)(unsigned));
    void write(std::span<const WidthSetting> settings);
    void setTone(const SubTone &tone);

    hal::RegisterBus &m_bus;
    const Tuning *m_tuning;
    std::uint32_t m_hz = 0;
    Bandwidth m_bandwidth = Bandwidth::Narrow;
    Mode m_mode = Mode::Idle;
    // The tone detectors switched on, and the flag of the one listened for.
    std::uint16_t m_detect = 0;
    std::uint16_t m_toneFlag = 0;
};

} // namespace dmm::drivers::radio
