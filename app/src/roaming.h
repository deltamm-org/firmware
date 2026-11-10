// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// The radio task's roaming: the roaming zone, the repeater in use, searches
// and moves. Runs in the radio task only, apart from what radio.h gives other
// tasks.

#include "dmm/services/codeplug.h"

namespace dmm::app::radio::roam {

enum class SearchCause : std::uint8_t
{
    Asked, // a key, the menu or the console
    Ptt,   // no repeater for a call
};

/// A roaming channel is in use.
bool channel();
/// Another kind of channel is in use now.
void leave();
/// The repeater in use, -1 for none.
int inUse();
/// Tuned to the channel's own frequency, or the repeater in use's: not
/// away on a search or a check.
bool home();
/// A channel is set: a roaming channel keeps the repeater in use, or starts
/// on the channel's own frequencies when they are one of the zone's.
void useChannel(const services::Channel &c);
/// Shows the repeater in use, or that a search runs.
void show(bool searching);

/// What the radio hears on the frequency it is tuned to: DMR slot timing;
/// its level, once the timing is verified; a burst with @p colourCode, of a
/// call (@p call) or of an idle transmission.
void slotTiming();
void level(int dbm);
void burst(std::uint8_t colourCode, bool call);
/// A call on the repeater in use goes on, or has just ended: no listening
/// until the gap between overs has passed from then.
void callHeard();

/// The repeater in use did not answer a call's wake-ups.
void unanswered();
/// Tries the roaming zone's repeaters, most likely first, until one is good,
/// and works through the best that answered. @p skipCurrent leaves out the
/// repeater in use, which has just failed to answer. True when one did.
bool search(SearchCause cause, bool skipCurrent = false);
/// A search asked for by the user.
void searchNow();
/// Debug feature: zone position @p position becomes the repeater in use.
void use(std::uint8_t position);
/// Debug feature: every repeater heard is forgotten.
void forget();
/// Auto roaming: whether the radio moves to a better repeater by itself.
void setAutomatic(bool on);
void setup(const services::RoamingSettings &settings);
void logStatus();
/// Roaming between calls; called on every turn of the radio task.
void step();

} // namespace dmm::app::radio::roam
