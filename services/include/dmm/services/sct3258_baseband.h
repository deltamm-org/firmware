// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/drivers/baseband/sct3258.h"
#include "dmm/services/baseband.h"

#include <array>
#include <optional>
#include <span>

namespace dmm::services {

/// The SCT3258 with an audio codec behind it, reached through the chip.
/// The chip sets the codec up once told which one it has; the board says
/// which, and which codec registers carry the speaker volume, the
/// microphone and the calibrated trims.
class Sct3258Baseband final : public Baseband
{
public:
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
        /// Connects the microphone for transmit, in order.
        std::span<const CodecWrite> transmit;
        /// The speaker volume register, its muted value and its value for
        /// each step 1..VolumeSteps.
        std::uint8_t speakerPage;
        std::uint8_t speakerRegister;
        std::uint8_t speakerMuted;
        std::array<std::uint8_t, VolumeSteps> speakerSteps;
        /// The registers the calibrated codec trims go to; the values here
        /// are not used.
        std::array<CodecWrite, 3> trims;
    };

    /// The radio's calibrated values for the chip and codec.
    struct Trims
    {
        /// The analog-mode gain words for ANALOG_CONFIG.
        std::array<std::uint8_t, 10> analogGains;
        /// Values for Audio::trims.
        std::array<std::uint8_t, 3> codec;
    };
    /// The calibrated values, or nothing without a calibration.
    using TrimsSource = std::optional<Trims> (*)();

    Sct3258Baseband(drivers::baseband::Sct3258 &chip, BasebandBoard &board, std::span<const std::uint8_t> bootBlob,
        const Audio &audio, TrimsSource trims);

    bool start() override;
    BasebandInfo info() override;
    bool setSpeakerVolume(int step) override;
    bool playTone(std::uint16_t hz, std::uint16_t ms, int percent) override;
    /// Also writes the codec trims, the first time after start().
    bool setAnalog(bool wide) override;
    bool setWorkMode(WorkMode mode) override;
    bool setCarrier(bool present) override;
    bool setMicrophone(bool on) override;
    /// "codec <page> <reg> <value>", "packet <type> <bytes>" (hex).
    bool debugCommand(std::string_view command) override;
    const char *debugHelp() const override;

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

    drivers::baseband::Sct3258 &m_chip;
    BasebandBoard &m_board;
    std::span<const std::uint8_t> m_blob;
    const Audio &m_audio;
    TrimsSource m_trims;
    BasebandInfo m_info;
    bool m_trimsWritten = false;
};

} // namespace dmm::services
