// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <algorithm>

namespace dmm::services::microphone_gain {

/// The microphone gain for transmit, in whole dB. StandardDb is the level
/// the board is set up for; each dB more or less changes the transmitted
/// voice by 1 dB. The board decides which stage takes it on each kind of
/// channel.
inline constexpr int MinDb = 0;
inline constexpr int StandardDb = 16;
/// Digital channels: the stage that takes the gain holds loud voices at
/// its target level.
inline constexpr int MaxDigitalDb = 30;
/// FM channels: the microphone may reach the modulator with no limiter in
/// between, so the range ends a few dB above the top level
/// (fromLevel(Levels)).
inline constexpr int MaxFmDb = 24;

/// Stored formats that keep the gain as a level 1..Levels give the levels
/// no unit. They are taken as these gains, StandardDb for the middle level.
inline constexpr int Levels = 5;
inline constexpr int LevelDb[Levels] = {12, 14, 16, 18, 21};

/// The gain for level @p level (1..Levels); a level out of range is the
/// middle one.
constexpr int fromLevel(int level)
{
    constexpr int Middle = (Levels + 1) / 2;
    return LevelDb[(level >= 1 && level <= Levels ? level : Middle) - 1];
}

/// @p db limited to the range on digital channels.
constexpr int clampDigital(int db)
{
    return std::clamp(db, MinDb, MaxDigitalDb);
}

/// @p db limited to the range on FM channels.
constexpr int clampFm(int db)
{
    return std::clamp(db, MinDb, MaxFmDb);
}

static_assert(fromLevel((Levels + 1) / 2) == StandardDb);
static_assert(fromLevel(1) >= MinDb && fromLevel(Levels) <= MaxFmDb && MaxFmDb <= MaxDigitalDb);

} // namespace dmm::services::microphone_gain
