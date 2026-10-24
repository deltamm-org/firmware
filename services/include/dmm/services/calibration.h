// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/storage.h"

#include <cstdint>
#include <optional>

namespace dmm::services {

/// Transmit power levels.
enum class PowerLevel : std::uint8_t
{
    Low = 0,
    Mid = 1,
    High = 2,
    Turbo = 3,
};

/// The radio's own calibration, kept in its storage. Read only. Values are
/// in the units the board's hardware takes them. Every value needs
/// present(); frequencies outside the calibrated bands give none.
class Calibration
{
public:
    /// FM transmit deviation settings: voice (with tones), CTCSS and DCS.
    struct Deviation
    {
        std::uint8_t voice;
        std::uint8_t ctcss;
        std::uint8_t dcs;
    };

    /// Reads the calibration. False when storage could not be read.
    virtual bool load(Storage &storage) = 0;
    /// Read, and the radio has been calibrated.
    virtual bool present() const = 0;
    /// Where the values were read from, for the log.
    virtual const char *source() const = 0;

    /// The battery reading's offset in millivolts.
    virtual std::optional<int> batteryOffsetMv() const = 0;
    /// Reference oscillator trim.
    virtual std::optional<std::uint16_t> referenceTrim() const = 0;
    /// Power amplifier control at @p hz and @p level.
    virtual std::optional<std::uint16_t> txPower(std::uint32_t hz, PowerLevel level) const = 0;
    /// Receive front-end tuning at @p hz.
    virtual std::optional<std::uint16_t> rxTune(std::uint32_t hz) const = 0;
    /// The level the squelch opens at, in -dBm, at @p hz for squelch level
    /// @p level (1..5; higher is tighter).
    virtual std::optional<int> squelchThreshold(std::uint32_t hz, int level) const = 0;
    virtual std::optional<Deviation> deviation() const = 0;

protected:
    ~Calibration() = default;
};

} // namespace dmm::services
