// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// What each row of the home screen shows.
struct HomeLayout
{
    /// The small line at the top, left of the clock.
    enum class Top : std::uint8_t
    {
        Talkgroup, // the DMR contact; "CHANNEL" without one
        Zone,
        Number, // the channel's place in its zone
        None,
        Count,
    };
    /// The large name.
    enum class Big : std::uint8_t
    {
        Channel,
        Contact, // the DMR contact's name
        Zone,
        Count,
    };
    /// The row under the name.
    enum class Middle : std::uint8_t
    {
        Frequency, // frequency, slot and colour code
        Talkgroup, // the DMR contact and a name
        Signal,    // a signal meter
        Count,
    };
    /// Next to the power.
    enum class Beside : std::uint8_t
    {
        Voltage,
        Volume,
        Signal,
        Count,
    };
    /// The bottom row.
    enum class Bottom : std::uint8_t
    {
        Details, // the repeater in use, the tones or the zone
        LastCaller,
        Position, // the GPS position
        None,
        Count,
    };

    Top top = Top::Talkgroup;
    bool clock = true;
    Big big = Big::Channel;
    Middle middle = Middle::Frequency;
    Beside beside = Beside::Voltage;
    Bottom bottom = Bottom::Details;

    bool operator==(const HomeLayout &) const = default;
    /// A row shows the signal level.
    bool showsSignal() const { return middle == Middle::Signal || beside == Beside::Signal; }
};

/// Whole layouts chosen at once. Own is any other layout.
enum class HomePreset : std::uint8_t
{
    Detailed, // the default layout
    Simple,   // the contact large, the last caller at the bottom
    Roaming,  // a signal meter under the name
    Own,
};

/// The layout of @p preset; Own gives the default layout.
HomeLayout presetLayout(HomePreset preset);
/// The preset @p layout is, or Own.
HomePreset presetOf(const HomeLayout &layout);

/// The signal meter: SignalBars bars, none lit at SignalFloorDbm or
/// below, all lit at SignalFullDbm or above.
inline constexpr int SignalBars = 10;
inline constexpr int SignalFloorDbm = -125;
inline constexpr int SignalFullDbm = -65;
/// The bars lit for a level of @p dbm, 0..SignalBars.
int signalBars(int dbm);

/// How long ago something happened, in the unit the screen shows it in.
struct Age
{
    enum class Unit : std::uint8_t
    {
        Now, // under a minute
        Minutes,
        Hours,
    };
    Unit unit = Unit::Now;
    std::uint32_t value = 0;

    bool operator==(const Age &) const = default;
};
Age ageOf(std::uint32_t seconds);

/// The values on the home screen that change by themselves, each to the
/// precision a row shows it.
struct HomeReadings
{
    /// The local time in minutes, or -1 while it is not known.
    std::int64_t clockMinutes = -1;
    bool signalKnown = false;
    int signalDbm = 0;
    /// The battery voltage in 10 mV steps, 0 while it is not known.
    int centivolts = 0;
    bool batteryLow = false;
    /// Volume step, -1 while it is not known.
    int volume = -1;
    /// The last call heard: when it ended (0 for none) and how long ago.
    std::uint32_t lastCallAt = 0;
    Age lastCallAge;
    /// The GPS position in 10^-5 degrees, when there is a fix.
    bool positionKnown = false;
    std::int32_t latitudeE5 = 0;
    std::int32_t longitudeE5 = 0;
};

/// Rows of the home screen, as bits.
namespace home_rows {
inline constexpr std::uint8_t Clock = 0x01;
inline constexpr std::uint8_t Middle = 0x02;
inline constexpr std::uint8_t Beside = 0x04;
inline constexpr std::uint8_t Bottom = 0x08;
} // namespace home_rows

/// The rows of @p layout that must be drawn again to go from showing
/// @p shown to @p now: only those whose text, meter or value changes.
std::uint8_t changedRows(const HomeLayout &layout, const HomeReadings &shown, const HomeReadings &now);

} // namespace dmm::services
