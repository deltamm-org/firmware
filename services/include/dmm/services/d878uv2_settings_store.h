// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/services/codeplug.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_preferences.h"
#include "dmm/services/settings_store.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services {

/// The settings store (settings_store.h) for the D878UVII's codeplug:
/// settings go to their own places in the codeplug, preferences to their
/// record. Once QuietMs have passed since the last change, the bytes that
/// differ from the codeplug are handed to the writer as pieces in rising
/// address order, as one write.
class D878Uv2SettingsStore
{
public:
    /// Bytes to write at a codeplug address.
    struct Piece
    {
        std::uint32_t address;
        std::span<const std::uint8_t> data;
    };
    /// Writes the pieces, in rising address order, as one write; Busy while
    /// the PC programs the radio.
    using Writer = lib::FnRef<SaveResult(std::span<const Piece>)>;

    static constexpr std::uint32_t QuietMs = 3000;
    /// Failed writes in a row after which the kept values are dropped.
    static constexpr unsigned MaxFailures = 3;
    /// Channels whose values are kept at once.
    static constexpr std::size_t MaxKeptChannels = 4;

    D878Uv2SettingsStore(D878Uv2Codeplug &codeplug, Writer write)
        : m_codeplug(codeplug), m_write(write)
    {
    }

    /// The fields written: squelch of both sides, key lock and auto lock,
    /// battery save, backlight time and level, GPS and its systems, the
    /// time zone and the roaming zone.
    void keepSettings(const CodeplugSettings &s, std::uint32_t nowMs);
    void keepPreferences(const RadioPreferences &p, std::uint32_t nowMs);
    void keepZone(bool sideB, std::uint8_t zone, std::uint16_t position, std::uint32_t nowMs);
    /// Kept for up to MaxKeptChannels channels until written; a later
    /// change on a kept channel replaces its values. A further channel
    /// writes what is kept at once, and is not kept if that write fails.
    void keepChannel(std::uint16_t index, PowerLevel power, SquelchMode squelch, std::uint32_t nowMs);
    /// The values kept for channel @p index and not yet written, if any.
    std::optional<KeptChannel> keptChannel(std::uint16_t index) const;

    bool pending() const { return m_pending; }
    /// Writes what is kept once QuietMs have passed since the last change.
    /// After Busy or Failed the values stay kept and are tried again
    /// QuietMs later; after MaxFailures failures in a row they are dropped.
    SaveResult flush(std::uint32_t nowMs);
    /// Writes what is kept now, without waiting for the quiet time.
    SaveResult flushNow() { return flush(m_changedAt + QuietMs); }

    void report() const;

private:
    struct Zone
    {
        std::uint8_t zone;
        std::uint16_t position;
    };
    struct ChannelValues
    {
        std::uint16_t index;
        PowerLevel power;
        SquelchMode squelch;
    };
    void changed(std::uint32_t nowMs);
    void drop();
    void addPiece(std::uint32_t address, std::span<const std::uint8_t> data);
    bool collect();

    D878Uv2Codeplug &m_codeplug;
    Writer m_write;
    bool m_pending = false;
    std::uint32_t m_changedAt = 0;
    unsigned m_failures = 0;
    std::optional<SaveResult> m_last;
    std::optional<CodeplugSettings> m_settings;
    std::optional<RadioPreferences> m_preferences;
    std::optional<Zone> m_zone[2];
    ChannelValues m_channels[MaxKeptChannels] = {};
    std::size_t m_channelCount = 0;

    // The codeplug's bytes as read, one part at a time.
    std::uint8_t m_read[0x100] = {};
    // The bytes being written; pieces point into them.
    std::uint8_t m_settingsBytes[0x100] = {};
    std::uint8_t m_extension[1] = {};
    std::uint8_t m_selection[2][2] = {};
    std::uint8_t m_preferenceBytes[d878uv2::PreferencesRecordBytes] = {};
    std::uint8_t m_channelBytes[MaxKeptChannels][0x40] = {};
    static constexpr std::size_t MaxPieces = 5 + MaxKeptChannels;
    Piece m_pieces[MaxPieces] = {};
    std::size_t m_pieceCount = 0;
};

static_assert(SettingsStore<D878Uv2SettingsStore>);

} // namespace dmm::services
