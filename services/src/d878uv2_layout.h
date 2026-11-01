// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// Where the D878UVII keeps each part of its codeplug, shared by the
// codeplug's reader and the settings store.

#include <cstdint>

namespace dmm::services::d878uv2 {

// Codeplug addresses.
constexpr std::uint32_t ChannelBanks = 0x00800000;
constexpr std::uint32_t ChannelBankStride = 0x00040000;
constexpr unsigned ChannelsPerBank = 128;
constexpr std::uint32_t ChannelBytes = 0x40;
// Each channel's extension record, this far after the channel's own.
constexpr std::uint32_t ChannelExtensionOffset = 0x2000;
// DeltaMM's preferences record (d878uv2_preferences.h): in the first
// channel bank's block, past the most its channels and their extensions
// take and before the block's copy markers, where codeplugs hold nothing.
constexpr std::uint32_t Preferences = 0x0081E000;
constexpr std::uint32_t ZoneChannels = 0x01000000;
constexpr std::uint32_t ZoneChannelStride = 0x200;
constexpr std::uint32_t Settings = 0x02500000;
constexpr std::uint32_t SettingsBytes = 0x100;
// The extended general settings, after the general settings.
constexpr std::uint32_t SettingsExtension = 0x02501400;
constexpr std::uint32_t SettingsExtensionBytes = 0x44;
constexpr std::uint32_t ZoneSelections = 0x02500100; // u16 list position per zone, side A then B
constexpr std::uint32_t ZoneSelectionSideB = 0x200;
constexpr std::uint32_t ZoneNames = 0x02540000;
constexpr std::uint32_t ZoneNameStride = 0x20;
constexpr std::uint32_t ZoneBitmap = 0x024C1300;
constexpr std::uint32_t ChannelBitmap = 0x024C1500;
constexpr std::uint32_t VfoA = 0x00FC0800;
constexpr std::uint32_t VfoB = 0x00FC0840;
constexpr std::uint32_t ContactBanks = 0x02680000;
constexpr std::uint32_t ContactBankStride = 0x00040000;
constexpr unsigned ContactsPerBank = 1000;
constexpr std::uint32_t ContactBytes = 0x64;
constexpr std::uint32_t ContactBitmap = 0x02640000;
constexpr unsigned MaxContacts = 10'000;
// Every contact's ID, sorted: 8-byte entries, the ID in BCD shifted left
// by one with the talkgroup flag in bit 0, then the contact index (both
// little-endian); unused entries are all ones.
constexpr std::uint32_t ContactIds = 0x04800000;
constexpr std::uint32_t ContactIdBytes = 8;
// Receive group lists: 0x200 bytes each, 64 little-endian contact indices
// first (unused ones all ones); a bitmap of the lists in use.
constexpr std::uint32_t GroupLists = 0x02980000;
constexpr std::uint32_t GroupListStride = 0x200;
constexpr std::uint32_t GroupListBitmap = 0x025C0B10;
constexpr unsigned MaxGroupLists = 250;
// The user directory: the number of users; their IDs sorted as in the
// contact ID table, the index being the user's place in the records; the
// records packed one after another across banks: call type, BCD ID, flags,
// then name, city and callsign, each zero-terminated.
constexpr std::uint32_t UserCount = 0x04840000;
constexpr unsigned MaxUsers = 500'000;
constexpr std::uint32_t UserIds = 0x04000000;
constexpr std::uint32_t UserIdsPerBank = 16'000;
constexpr std::uint32_t UserRecords = 0x05500000;
constexpr std::uint32_t UserRecordBank = 100'000;
constexpr std::uint32_t UserBankStride = 0x00040000;
constexpr std::uint32_t UserHeaderBytes = 6;
constexpr std::uint32_t UserCityBytes = 15;
constexpr std::uint32_t RadioIds = 0x02580000;
constexpr std::uint32_t RadioIdBytes = 0x20;
constexpr std::uint32_t RadioIdBitmap = 0x024C1320;
constexpr unsigned MaxRadioIds = 250;
// Roaming repeaters, 0x20 bytes each: receive and transmit frequency as
// eight BCD digits of 10 Hz, big-endian; the colour code (16 = the
// channel's own); the time slot; the name. Roaming zones, 0x80 bytes each:
// 64 repeater indices (0xFF unused), then the name. Each has a bitmap of
// those in use.
constexpr std::uint32_t RoamingRepeaters = 0x01040000;
constexpr std::uint32_t RoamingRepeaterBytes = 0x20;
constexpr std::uint32_t RoamingRepeaterName = 0x0A;
constexpr std::uint32_t RoamingRepeaterBitmap = 0x01042000;
constexpr unsigned MaxRoamingRepeaters = 250;
constexpr std::uint32_t RoamingZones = 0x01043000;
constexpr std::uint32_t RoamingZoneBytes = 0x80;
constexpr std::uint32_t RoamingZoneBitmap = 0x01042080;
constexpr std::uint32_t RoamingZoneName = 0x40;

} // namespace dmm::services::d878uv2
