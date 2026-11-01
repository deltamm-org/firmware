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
/// between, so the range ends at the top level (fromLevel(Levels - 1)).
inline constexpr int MaxFmDb = 24;

/// Stored formats that keep the gain as a level 0..Levels-1 (shown one
/// more) give the levels no unit. They are taken as StandardDb for the
/// middle level and LevelStepDb more for each level above it: a choice,
/// not a measurement.
inline constexpr int Levels = 5;
inline constexpr int MiddleLevel = Levels / 2;
inline constexpr int LevelStepDb = 4;

/// The gain for level @p level (0..Levels-1); a level out of range is the
/// middle one.
constexpr int fromLevel(int level)
{
    return StandardDb + ((level >= 0 && level < Levels ? level : MiddleLevel) - MiddleLevel) * LevelStepDb;
}

/// The level whose gain is nearest @p db; the lower one when two are as
/// near.
constexpr int nearestLevel(int db)
{
    const auto distance = [db](int level) { return fromLevel(level) > db ? fromLevel(level) - db : db - fromLevel(level); };
    int nearest = 0;
    for (int level = 1; level < Levels; ++level) {
        if (distance(level) < distance(nearest)) {
            nearest = level;
        }
    }
    return nearest;
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

static_assert(fromLevel(MiddleLevel) == StandardDb);
static_assert(fromLevel(Levels - 1) == MaxFmDb);

} // namespace dmm::services::microphone_gain
