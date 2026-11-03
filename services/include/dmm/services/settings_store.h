// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/codeplug.h"

#include <concepts>
#include <cstdint>
#include <optional>

namespace dmm::services {

enum class SaveResult : std::uint8_t
{
    Saved,
    /// Nothing was due, or nothing differed from the codeplug.
    Unchanged,
    /// The codeplug is being written from the PC; tried again later.
    Busy,
    Failed,
};

/// A memory channel's values kept by the store and not yet written.
struct KeptChannel
{
    PowerLevel power;
    SquelchMode squelchMode;
};

/// Keeps settings changed on the radio in its codeplug, so they hold after
/// a restart; the board names it (dmm/board/parts.h). Values come in as
/// they change and are written together once none has changed for a
/// while, so a run of changes costs one write and an unchanged value none.
/// Never written while the PC programs the radio.
///
/// - keepSettings(s, nowMs): the general settings as they are now; every
///   field the radio changes must hold its current value.
/// - keepPreferences(p, nowMs): DeltaMM's own preferences, all of them.
/// - keepZone(sideB, zone, position, nowMs): a side's zone and its place
///   in the zone's channel list.
/// - keepChannel(index, power, squelch, nowMs): a memory channel's power
///   and squelch mode.
/// - keptChannel(index): the values kept for that channel and not yet
///   written, if any; a channel read from the codeplug takes them over
///   its own.
/// - pending(): something is kept and not yet written.
/// - flush(nowMs): writes what is due; called often from one task.
/// - report(): logs what is kept and the last write's outcome.
template <typename T>
concept SettingsStore = requires(T &s, const CodeplugSettings &settings, const RadioPreferences &preferences, bool sideB,
    std::uint8_t zone, std::uint16_t index, PowerLevel power, SquelchMode squelch, std::uint32_t nowMs) {
    s.keepSettings(settings, nowMs);
    s.keepPreferences(preferences, nowMs);
    s.keepZone(sideB, zone, index, nowMs);
    s.keepChannel(index, power, squelch, nowMs);
    { s.keptChannel(index) } -> std::same_as<std::optional<KeptChannel>>;
    { s.pending() } -> std::same_as<bool>;
    { s.flush(nowMs) } -> std::same_as<SaveResult>;
    s.report();
};

} // namespace dmm::services
