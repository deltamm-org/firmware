// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/codeplug.h"

#include <cstdint>

namespace dmm::services {

/// The radio's working channel: a memory channel picked by zone and
/// position, or the VFO tuned to any frequency. Starts where the codeplug
/// left the main side. Nothing is written back.
class ChannelSelector
{
public:
    static constexpr std::size_t NameBytes = CodeplugSource::ZoneNameBytes + 1;

    explicit ChannelSelector(CodeplugSource &codeplug)
        : m_codeplug(codeplug)
    {
    }

    /// Reads the settings and the main side's zone and channel. False when
    /// the codeplug has no usable zone (the VFO is then used).
    bool load();

    const CodeplugSettings &settings() const { return m_settings; }
    bool vfoMode() const { return m_vfo; }
    /// The channel to use now, if any.
    const std::optional<Channel> &current() const { return m_current; }

    std::uint8_t zone() const { return m_zone; }
    const char *zoneName() const { return m_zoneName; }
    /// 1-based position in the zone and the zone's size.
    std::size_t position() const { return m_position + 1; }
    std::size_t zoneSize() const { return m_count; }

    /// Moves @p steps channels on in the zone, wrapping; skips channels
    /// the codeplug cannot read. False when the zone is empty.
    bool step(int steps);
    /// Moves to the next or previous zone in use, keeping that zone's last
    /// selected channel.
    bool stepZone(int steps);
    bool selectZone(std::uint8_t zone);
    /// Every zone in use; returns how many.
    std::size_t zones(std::span<std::uint8_t> out) { return m_codeplug.zonesInUse(out); }
    /// A zone's name, zero-terminated (empty if unreadable).
    void zoneLabel(std::uint8_t zone, std::span<char, NameBytes> name) { m_codeplug.zoneName(zone, name); }

    /// Squelch level 0..5 of the main side, until the codeplug is read again.
    void setSquelch(std::uint8_t level);
    std::uint8_t squelch() const { return m_sideB ? m_settings.squelchB : m_settings.squelchA; }
    /// Transmit power of the current channel, until the channel changes.
    void setPower(PowerLevel level);
    /// Squelch mode of the current channel, until the channel changes.
    void setSquelchMode(SquelchMode mode);

    /// Switches to the VFO at @p hz, with the VFO record's other settings.
    void tune(std::uint32_t hz);
    /// Back to the memory channel.
    bool memoryMode();

private:
    bool loadZone(std::uint8_t zone, std::uint16_t position);
    bool loadChannel();

    CodeplugSource &m_codeplug;
    CodeplugSettings m_settings;
    bool m_sideB = false;
    bool m_vfo = false;
    std::uint8_t m_zone = 0;
    char m_zoneName[NameBytes] = {};
    std::uint16_t m_channels[CodeplugSource::MaxZoneChannels] = {};
    std::size_t m_count = 0;
    std::size_t m_position = 0;
    std::optional<Channel> m_current;
};

} // namespace dmm::services
