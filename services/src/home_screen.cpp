// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/home_screen.h"

#include <initializer_list>

namespace dmm::services {

HomeLayout presetLayout(HomePreset preset)
{
    HomeLayout l;
    switch (preset) {
    case HomePreset::Simple:
        l.top = HomeLayout::Top::None;
        l.big = HomeLayout::Big::Contact;
        l.middle = HomeLayout::Middle::Talkgroup;
        l.bottom = HomeLayout::Bottom::LastCaller;
        break;
    case HomePreset::Roaming:
        l.middle = HomeLayout::Middle::Signal;
        break;
    case HomePreset::Detailed:
    case HomePreset::Own:
        break;
    }
    return l;
}

HomePreset presetOf(const HomeLayout &layout)
{
    for (const HomePreset p : {HomePreset::Detailed, HomePreset::Simple, HomePreset::Roaming}) {
        if (presetLayout(p) == layout) {
            return p;
        }
    }
    return HomePreset::Own;
}

int signalBars(int dbm)
{
    if (dbm <= SignalFloorDbm) {
        return 0;
    }
    if (dbm >= SignalFullDbm) {
        return SignalBars;
    }
    return (dbm - SignalFloorDbm) * SignalBars / (SignalFullDbm - SignalFloorDbm);
}

Age ageOf(std::uint32_t seconds)
{
    if (seconds < 60) {
        return {};
    }
    if (seconds < 3600) {
        return {Age::Unit::Minutes, seconds / 60};
    }
    return {Age::Unit::Hours, seconds / 3600};
}

std::uint8_t changedRows(const HomeLayout &layout, const HomeReadings &shown, const HomeReadings &now)
{
    const bool signal = shown.signalKnown != now.signalKnown || (now.signalKnown && shown.signalDbm != now.signalDbm);
    std::uint8_t rows = 0;
    if (layout.clock && shown.clockMinutes != now.clockMinutes) {
        rows |= home_rows::Clock;
    }
    if (layout.middle == HomeLayout::Middle::Signal && signal) {
        rows |= home_rows::Middle;
    }
    bool beside = false;
    switch (layout.beside) {
    case HomeLayout::Beside::Voltage:
        beside = shown.centivolts != now.centivolts || shown.batteryLow != now.batteryLow;
        break;
    case HomeLayout::Beside::Volume:
        beside = shown.volume != now.volume;
        break;
    case HomeLayout::Beside::Signal:
        beside = signal;
        break;
    case HomeLayout::Beside::Count:
        break;
    }
    if (beside) {
        rows |= home_rows::Beside;
    }
    bool bottom = false;
    switch (layout.bottom) {
    case HomeLayout::Bottom::LastCaller:
        bottom = shown.lastCallAt != now.lastCallAt || (now.lastCallAt != 0 && shown.lastCallAge != now.lastCallAge);
        break;
    case HomeLayout::Bottom::Position:
        bottom = shown.positionKnown != now.positionKnown ||
            (now.positionKnown && (shown.latitudeE5 != now.latitudeE5 || shown.longitudeE5 != now.longitudeE5));
        break;
    case HomeLayout::Bottom::Details:
    case HomeLayout::Bottom::None:
    case HomeLayout::Bottom::Count:
        break;
    }
    if (bottom) {
        rows |= home_rows::Bottom;
    }
    return rows;
}

} // namespace dmm::services
