// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/channel_selector.h"

namespace dmm::services {

bool ChannelSelector::load()
{
    if (const auto s = m_codeplug.settings()) {
        m_settings = *s;
    }
    m_sideB = m_settings.sideBMain;
    const std::uint8_t zone = m_sideB ? m_settings.zoneB : m_settings.zoneA;
    const bool zoneOk = loadZone(zone, m_codeplug.zoneSelection(zone, m_sideB)) || stepZone(1);
    if (!zoneOk || (m_sideB ? m_settings.vfoB : m_settings.vfoA)) {
        const auto v = m_codeplug.vfo(m_sideB);
        tune(v ? v->receiveHz : 0);
    }
    return zoneOk;
}

bool ChannelSelector::loadZone(std::uint8_t zone, std::uint16_t position)
{
    if (!m_codeplug.zoneInUse(zone)) {
        return false;
    }
    const std::size_t n = m_codeplug.zoneChannels(zone, m_channels);
    if (n == 0) {
        return false;
    }
    m_zone = zone;
    m_count = n;
    m_codeplug.zoneName(zone, std::span<char, NameBytes>(m_zoneName));
    m_position = position < n ? position : 0;
    if (!m_vfo) {
        return loadChannel() || step(1);
    }
    return true;
}

bool ChannelSelector::loadChannel()
{
    m_current = m_codeplug.channel(m_channels[m_position]);
    return m_current.has_value();
}

bool ChannelSelector::step(int steps)
{
    if (m_count == 0) {
        return false;
    }
    m_vfo = false;
    if (steps == 0) {
        return loadChannel();
    }
    const bool forward = steps > 0;
    for (int moves = forward ? steps : -steps; moves > 0; --moves) {
        // Land on the next readable channel; give up after a full turn of
        // unreadable ones.
        std::size_t tries = 0;
        do {
            m_position = forward ? (m_position + 1) % m_count : (m_position + m_count - 1) % m_count;
            ++tries;
        } while (!loadChannel() && tries < m_count);
        if (!m_current) {
            return false;
        }
    }
    return true;
}

bool ChannelSelector::stepZone(int steps)
{
    const bool forward = steps >= 0;
    int zone = m_zone;
    for (int moves = forward ? (steps == 0 ? 1 : steps) : -steps; moves > 0; --moves) {
        bool found = false;
        for (unsigned tries = 0; tries < CodeplugSource::MaxZones && !found; ++tries) {
            zone = (zone + (forward ? 1 : CodeplugSource::MaxZones - 1)) % CodeplugSource::MaxZones;
            const auto z = static_cast<std::uint8_t>(zone);
            found = loadZone(z, m_codeplug.zoneSelection(z, m_sideB));
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool ChannelSelector::selectZone(std::uint8_t zone)
{
    return loadZone(zone, m_codeplug.zoneSelection(zone, m_sideB));
}

void ChannelSelector::setSquelch(std::uint8_t level)
{
    (m_sideB ? m_settings.squelchB : m_settings.squelchA) = level > 5 ? 5 : level;
}

void ChannelSelector::setPower(PowerLevel level)
{
    if (m_current) {
        m_current->power = level;
    }
}

void ChannelSelector::tune(std::uint32_t hz)
{
    Channel c;
    if (const auto v = m_codeplug.vfo(m_sideB)) {
        c = *v;
    }
    const std::int64_t shift = static_cast<std::int64_t>(c.transmitHz) - c.receiveHz;
    c.receiveHz = hz;
    c.transmitHz = static_cast<std::uint32_t>(hz + shift);
    c.mode = ChannelMode::Analog;
    c.name[0] = '\0';
    m_current = c;
    m_vfo = true;
}

bool ChannelSelector::memoryMode()
{
    if (m_count == 0) {
        return false;
    }
    m_vfo = false;
    return loadChannel() || step(1);
}

} // namespace dmm::services
