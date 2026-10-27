// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/drivers/baseband/sct3258.h"
#include "dmm/services/baseband.h"

#include <array>
#include <optional>
#include <span>

namespace dmm::services {

/// The SCT3258 with an audio codec behind it, reached through the chip; a
/// Baseband.
/// The chip sets the codec up once told which one it has; the board says
/// which, and which codec registers carry the speaker volume, the
/// microphone and the calibrated trims.
class Sct3258Baseband
{
public:
    /// One codec register: page and register.
    struct CodecRegister
    {
        std::uint8_t page;
        std::uint8_t reg;
    };
    /// One codec register write: page, register, value.
    struct CodecWrite
    {
        std::uint8_t page;
        std::uint8_t reg;
        std::uint8_t value;
    };

    /// The codec as the board wires it.
    struct Audio
    {
        /// The chip's code for the codec (Sct3258::Codec...).
        std::uint8_t codec;
        /// Receive levels with the microphone off: at start and after
        /// transmit.
        std::span<const CodecWrite> receive;
        /// Receive levels on digital channels, where the voice comes from
        /// the chip's vocoder.
        std::span<const CodecWrite> digitalReceive;
        /// Connects the microphone for transmit, in order.
        std::span<const CodecWrite> transmit;
        /// The speaker volume register, its muted value and its value for
        /// each step 1..SpeakerVolumeSteps.
        std::uint8_t speakerPage;
        std::uint8_t speakerRegister;
        std::uint8_t speakerMuted;
        std::array<std::uint8_t, SpeakerVolumeSteps> speakerSteps;
        /// The registers the calibrated codec trims go to.
        std::array<CodecRegister, 3> trims;
        /// SQ_LEVEL for the chip's own carrier squelch, on digital and on
        /// analog channels: gain adjust, LNA, forced RF gain, check
        /// interval, then the digital and analog noise thresholds (high,
        /// low; big-endian words). Empty: the chip's defaults.
        std::span<const std::uint8_t> digitalSquelch;
        std::span<const std::uint8_t> analogSquelch;
        /// The board's FM demodulator output reaches the chip with its
        /// polarity reversed; digital receive tells the chip so.
        bool demodulatorInverted;
    };

    /// The radio's calibrated values for the chip and codec.
    struct Trims
    {
        /// The analog-mode gain words for ANALOG_CONFIG.
        std::array<std::uint8_t, 10> analogGains;
        /// Values for Audio::trims.
        std::array<std::uint8_t, 3> codec;
    };
    /// The calibrated values, or nothing until a calibration has been read.
    using TrimsSource = std::optional<Trims> (*)();

    /// The radio's calibrated values for digital receive at one frequency.
    struct DigitalTrims
    {
        /// DC_OFFSET: converter offsets and output gains.
        std::array<std::uint8_t, 12> dcOffset;
        /// DEMOD_GAIN.
        std::array<std::uint8_t, 2> demodGain;
        /// MOD_GAIN, for transmit.
        std::array<std::uint8_t, 2> modGain;
        /// DIGI_SPEAKER_GAIN and DIGI_MIC_GAIN; nothing keeps the chip's
        /// own.
        std::optional<std::array<std::uint8_t, 2>> speakerGain;
        std::optional<std::array<std::uint8_t, 2>> micGain;
        /// DMR_OFFSET: transmit slot timing.
        std::optional<std::array<std::uint8_t, 8>> slotTiming;
    };
    using DigitalTrimsSource = std::optional<DigitalTrims> (*)(std::uint32_t hz);

    Sct3258Baseband(drivers::baseband::Sct3258 &chip, BasebandBoard &board, std::span<const std::uint8_t> bootBlob,
        const Audio &audio, TrimsSource trims, DigitalTrimsSource digitalTrims = nullptr);

    /// Also writes the codec trims if the calibration has been read.
    bool start();
    BasebandInfo info();
    bool setSpeakerVolume(int step);
    bool playTone(std::uint16_t hz, std::uint16_t ms, int percent);
    /// Also writes the codec trims if start() could not.
    bool setAnalog(bool wide);
    bool setWorkMode(BasebandWorkMode mode);
    bool setCarrier(bool present);
    bool setMicrophone(bool on);
    /// Also switches the vocoder over (a few seconds the first time) and
    /// sets the DMR call options once after start().
    bool setDigital(const dmr::ChannelSettings &settings, std::uint32_t receiveHz, bool repeater);
    bool nextEvent(dmr::Event &event);
    bool startDigitalCall(const dmr::ChannelSettings &settings, const dmr::FullLc &lc, std::uint32_t transmitHz);
    bool endDigitalCall(const dmr::ChannelSettings &settings, const dmr::FullLc &lc);
    bool wakeRepeater(const dmr::ChannelSettings &settings, std::uint32_t source);
    /// "codec <page> <reg> <value>", "packet <type> <bytes>" (hex).
    bool debugCommand(std::string_view command);
    const char *debugHelp() const;

    /// Writes one codec register; false unless running and accepted.
    bool codecWrite(std::uint8_t page, std::uint8_t reg, std::uint8_t value);
    /// Sends one raw packet, then logs every packet the chip sends within
    /// @p waitMs (at most four); returns how many.
    int exchange(std::uint8_t type, std::span<const std::uint8_t> fields, unsigned waitMs);

    /// The speaker register value for a volume step.
    std::uint8_t volumeRegister(int step) const;

private:
    bool fail(const char *problem);
    bool write(std::span<const CodecWrite> writes);
    /// Hands over the first frame after the switch to transmit, again if
    /// the chip does not take it.
    bool sendFirstFrame(std::uint8_t colourCode, std::uint8_t property, std::span<const std::uint8_t> data);
    /// Tells the chip the carrier is gone, then puts it in transmit mode.
    bool leaveReceive();
    bool enterTransmit();
    /// The codec's receive levels for the current mode.
    std::span<const CodecWrite> receiveWrites() const;
    void writeTrims(const Trims &trims);
    bool selectVocoder(std::uint8_t vocoder);
    bool toEvent(const drivers::baseband::Packet &report, dmr::Event &event) const;

    drivers::baseband::Sct3258 &m_chip;
    BasebandBoard &m_board;
    std::span<const std::uint8_t> m_blob;
    const Audio &m_audio;
    TrimsSource m_trims;
    DigitalTrimsSource m_digitalTrims;
    BasebandInfo m_info;
    bool m_trimsWritten = false;
    std::uint8_t m_vocoder = 0;
    bool m_callOptionsSet = false;
    // The last digital receive was through a repeater.
    bool m_repeater = false;
};

} // namespace dmm::services
