// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_settings_store.h"

#include "d878uv2_layout.h"
#include "dmm/services/log.h"

#include <algorithm>
#include <iterator>

namespace dmm::services {

using namespace d878uv2;

namespace {

// The preferences record stays clear of the first bank's channels and
// their extensions, and of the block's copy markers.
static_assert(Preferences >= ChannelBanks + ChannelExtensionOffset + ChannelsPerBank * ChannelBytes);
static_assert(Preferences + PreferencesRecordBytes <= ChannelBanks + 0x20000 - 8);

// Backlight times by their codeplug index, in seconds.
constexpr std::uint16_t BacklightTimes[] = {0, 5, 10, 15, 20, 25, 30, 60, 120, 180, 240, 300, 900, 1800, 2700, 3600};
// The GPS systems in the extended general settings.
constexpr std::uint32_t GpsSystemsAt = SettingsExtension + 0x35;

std::uint8_t clampTo(int v, int low, int high)
{
    return static_cast<std::uint8_t>(std::clamp(v, low, high));
}

// The general settings' bytes for the values the radio changes; the other
// bytes stay as they are.
void encodeSettings(const CodeplugSettings &s, std::span<std::uint8_t, 0x100> b)
{
    b[0x09] = clampTo(s.squelchA, 0, 5);
    b[0x0A] = clampTo(s.squelchB, 0, 5);
    b[0xBE] = static_cast<std::uint8_t>((b[0xBE] & ~0x1Bu) | (s.keyLock.knob ? 0x01u : 0u) |
        (s.keyLock.keypad ? 0x02u : 0u) | (s.keyLock.side ? 0x08u : 0u) | (s.keyLock.forced ? 0x10u : 0u));
    // Auto key lock follows the menu exit time: 5 s steps from 5 s.
    b[0x02] = s.keyLock.autoLockMs != 0 ? 1 : 0;
    if (s.keyLock.autoLockMs != 0) {
        b[0x37] = clampTo(static_cast<int>((s.keyLock.autoLockMs + 2500) / 5000) - 1, 0, 11);
    }
    b[0x0B] = clampTo(s.batterySave, 0, 2);
    const auto light = std::find(std::begin(BacklightTimes), std::end(BacklightTimes), s.backlightS);
    if (light != std::end(BacklightTimes)) {
        b[0x27] = static_cast<std::uint8_t>(light - std::begin(BacklightTimes));
    }
    b[0x26] = clampTo(s.backlightLevel - 1, 0, 4);
    b[0x28] = s.gps ? 1 : 0;
    // The time zone in half hours from UTC-12:00 (0).
    b[0x30] = clampTo(s.timeZoneMinutes / 30 + 24, 0, 52);
    b[0xDC] = s.roaming.zone;
}

std::uint32_t channelAddress(std::uint16_t index)
{
    return ChannelBanks + (index / ChannelsPerBank) * ChannelBankStride + (index % ChannelsPerBank) * ChannelBytes;
}

const char *resultText(SaveResult r)
{
    switch (r) {
    case SaveResult::Saved:
        return "saved";
    case SaveResult::Unchanged:
        return "unchanged";
    case SaveResult::Busy:
        return "busy (PC programming)";
    case SaveResult::Failed:
        return "failed";
    }
    return "?";
}

} // namespace

void D878Uv2SettingsStore::changed(std::uint32_t nowMs)
{
    m_pending = true;
    m_changedAt = nowMs;
}

void D878Uv2SettingsStore::drop()
{
    m_pending = false;
    m_failures = 0;
    m_settings.reset();
    m_preferences.reset();
    m_zone[0].reset();
    m_zone[1].reset();
    m_channelCount = 0;
}

void D878Uv2SettingsStore::keepSettings(const CodeplugSettings &s, std::uint32_t nowMs)
{
    m_settings = s;
    changed(nowMs);
}

void D878Uv2SettingsStore::keepPreferences(const RadioPreferences &p, std::uint32_t nowMs)
{
    m_preferences = p;
    changed(nowMs);
}

void D878Uv2SettingsStore::keepZone(bool sideB, std::uint8_t zone, std::uint16_t position, std::uint32_t nowMs)
{
    m_zone[sideB ? 1 : 0] = Zone{zone, position};
    changed(nowMs);
}

void D878Uv2SettingsStore::keepChannel(std::uint16_t index, PowerLevel power, SquelchMode squelch, std::uint32_t nowMs)
{
    if (index >= CodeplugSource::MaxChannels) {
        return;
    }
    const auto end = m_channels + m_channelCount;
    auto kept = std::find_if(m_channels, end, [index](const ChannelValues &c) { return c.index == index; });
    if (kept == end) {
        if (m_channelCount == MaxKeptChannels) {
            const SaveResult r = flushNow();
            if (m_channelCount == MaxKeptChannels) {
                log::warning("settings") << "channel " << index + 1 << " not kept: write " << resultText(r);
                return;
            }
        }
        kept = &m_channels[m_channelCount++];
    }
    *kept = ChannelValues{index, power, squelch};
    changed(nowMs);
}

std::optional<KeptChannel> D878Uv2SettingsStore::keptChannel(std::uint16_t index) const
{
    const auto end = m_channels + m_channelCount;
    const auto kept = std::find_if(m_channels, end, [index](const ChannelValues &c) { return c.index == index; });
    if (kept == end) {
        return std::nullopt;
    }
    return KeptChannel{kept->power, kept->squelch};
}

void D878Uv2SettingsStore::addPiece(std::uint32_t address, std::span<const std::uint8_t> data)
{
    if (m_pieceCount < MaxPieces) {
        m_pieces[m_pieceCount++] = Piece{address, data};
    }
}

// Puts the bytes that differ from the codeplug into pieces; false when
// the codeplug could not be read.
bool D878Uv2SettingsStore::collect()
{
    m_pieceCount = 0;
    if (m_settings || m_zone[0] || m_zone[1]) {
        const std::span read(m_read, sizeof m_settingsBytes);
        if (!readable(m_codeplug.read(Settings, read))) {
            return false;
        }
        std::copy(read.begin(), read.end(), std::begin(m_settingsBytes));
        if (m_settings) {
            encodeSettings(*m_settings, m_settingsBytes);
        }
        for (int side = 0; side < 2; ++side) {
            if (m_zone[side]) {
                m_settingsBytes[side == 0 ? 0x1F : 0x20] = m_zone[side]->zone;
            }
        }
        if (!std::equal(read.begin(), read.end(), std::begin(m_settingsBytes))) {
            addPiece(Settings, m_settingsBytes);
        }
    }
    for (int side = 0; side < 2; ++side) {
        if (!m_zone[side] || m_zone[side]->zone >= CodeplugSource::MaxZones) {
            continue;
        }
        const std::uint32_t at = ZoneSelections + (side == 1 ? ZoneSelectionSideB : 0) + m_zone[side]->zone * 2u;
        std::uint8_t read[2];
        if (!readable(m_codeplug.read(at, read))) {
            return false;
        }
        m_selection[side][0] = static_cast<std::uint8_t>(m_zone[side]->position);
        m_selection[side][1] = static_cast<std::uint8_t>(m_zone[side]->position >> 8);
        if (read[0] != m_selection[side][0] || read[1] != m_selection[side][1]) {
            addPiece(at, m_selection[side]);
        }
    }
    if (m_settings) {
        // The mode 0..6 is one less than the systems' bits.
        std::uint8_t read = 0;
        if (!readable(m_codeplug.read(GpsSystemsAt, std::span(&read, 1)))) {
            return false;
        }
        m_extension[0] = clampTo(m_settings->gpsSystems - 1, 0, 6);
        if (read != m_extension[0]) {
            addPiece(GpsSystemsAt, m_extension);
        }
    }
    if (m_preferences) {
        const std::span read(m_read, sizeof m_preferenceBytes);
        if (!readable(m_codeplug.read(Preferences, read))) {
            return false;
        }
        encodePreferences(*m_preferences, m_preferenceBytes);
        if (!std::equal(read.begin(), read.end(), std::begin(m_preferenceBytes))) {
            addPiece(Preferences, m_preferenceBytes);
        }
    }
    for (std::size_t i = 0; i < m_channelCount; ++i) {
        const ChannelValues &c = m_channels[i];
        auto &bytes = m_channelBytes[i];
        const std::uint32_t at = channelAddress(c.index);
        const std::span read(m_read, sizeof bytes);
        if (!readable(m_codeplug.read(at, read))) {
            return false;
        }
        std::copy(read.begin(), read.end(), std::begin(bytes));
        // Power at bits 2..3; squelch at bits 4..6: 0 carrier, 1 the
        // receive tone.
        bytes[0x08] = static_cast<std::uint8_t>((read[0x08] & ~0x0Cu) | (static_cast<unsigned>(c.power) & 3u) << 2);
        bytes[0x19] =
            static_cast<std::uint8_t>((read[0x19] & ~0x70u) | (c.squelch == SquelchMode::Tone ? 0x10u : 0u));
        if (!std::equal(read.begin(), read.end(), std::begin(bytes))) {
            addPiece(at, bytes);
        }
    }
    std::sort(m_pieces, m_pieces + m_pieceCount, [](const Piece &a, const Piece &b) { return a.address < b.address; });
    return true;
}

SaveResult D878Uv2SettingsStore::flush(std::uint32_t nowMs)
{
    if (!m_pending || static_cast<std::int32_t>(nowMs - m_changedAt) < static_cast<std::int32_t>(QuietMs)) {
        return SaveResult::Unchanged;
    }
    SaveResult r = SaveResult::Failed;
    if (collect()) {
        r = m_pieceCount == 0 ? SaveResult::Unchanged : m_write(std::span<const Piece>(m_pieces, m_pieceCount));
    }
    m_last = r;
    if (r == SaveResult::Saved || r == SaveResult::Unchanged) {
        drop();
    } else if (r == SaveResult::Failed && ++m_failures >= MaxFailures) {
        log::warning("settings") << "not kept: " << MaxFailures << " writes failed";
        drop();
    } else {
        m_changedAt = nowMs;
    }
    return r;
}

void D878Uv2SettingsStore::report() const
{
    auto line = log::info("settings");
    if (!m_pending) {
        line << "nothing pending";
    } else {
        line << "pending:" << (m_settings ? " settings" : "") << (m_preferences ? " preferences" : "");
        for (int side = 0; side < 2; ++side) {
            if (m_zone[side]) {
                line << " zone " << (side == 0 ? 'A' : 'B') << '=' << m_zone[side]->zone << '/'
                     << m_zone[side]->position + 1;
            }
        }
        for (std::size_t i = 0; i < m_channelCount; ++i) {
            line << " channel " << m_channels[i].index + 1;
        }
        line << ", changed at " << m_changedAt << " ms";
    }
    line << "; last write " << (m_last ? resultText(*m_last) : "none");
}

} // namespace dmm::services
