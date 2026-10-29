// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_codeplug.h"

#include <algorithm>
#include <iterator>

namespace dmm::services {

namespace {

// Codeplug addresses.
constexpr std::uint32_t ChannelBanks = 0x00800000;
constexpr std::uint32_t ChannelBankStride = 0x00040000;
constexpr unsigned ChannelsPerBank = 128;
constexpr std::uint32_t ChannelBytes = 0x40;
constexpr std::uint32_t ZoneChannels = 0x01000000;
constexpr std::uint32_t ZoneChannelStride = 0x200;
constexpr std::uint32_t Settings = 0x02500000;
constexpr std::uint32_t SettingsBytes = 0x100;
// The extended general settings, after the general settings.
constexpr std::uint32_t SettingsExtension = 0x02501400;
constexpr std::uint32_t SettingsExtensionBytes = 0x40;
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
// Programmable key function codes.
constexpr std::uint8_t KeyRoam = 0x24;

constexpr std::uint32_t BlockBytes = 0x20000;
constexpr std::uint32_t MarkerOffset = BlockBytes - 8;

// CTCSS tones by their codeplug index, in 0.1 Hz; index 51 is the
// channel's own tone.
constexpr std::uint16_t CtcssTable[] = {625, 670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000,
    1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713,
    1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503,
    2541};
constexpr std::uint8_t CustomCtcss = 51;

// An ID as BCD digits, which sort like the number.
std::uint32_t bcdKey(std::uint32_t id)
{
    std::uint32_t key = 0;
    for (std::uint32_t v = id, shift = 0; shift < 32; v /= 10, shift += 4) {
        key |= (v % 10) << shift;
    }
    return key;
}

std::uint16_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(le16(p)) | static_cast<std::uint32_t>(le16(p + 2)) << 16;
}

// Big-endian BCD, eight digits.
std::optional<std::uint32_t> bcd8(const std::uint8_t *p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        for (const unsigned digit : {static_cast<unsigned>(p[i] >> 4), static_cast<unsigned>(p[i] & 0xF)}) {
            if (digit > 9) {
                return std::nullopt;
            }
            v = v * 10 + digit;
        }
    }
    return v;
}

Tone decodeTone(unsigned kind, std::uint8_t ctcssIndex, std::uint16_t dcs, std::uint16_t customCtcss)
{
    Tone t;
    if (kind == 1) {
        if (ctcssIndex < CustomCtcss) {
            t.kind = Tone::Kind::Ctcss;
            t.value = CtcssTable[ctcssIndex];
        } else if (ctcssIndex == CustomCtcss && customCtcss != 0) {
            t.kind = Tone::Kind::Ctcss;
            t.value = customCtcss;
        }
    } else if (kind == 2) {
        t.kind = Tone::Kind::Dcs;
        t.inverted = dcs >= 512;
        t.value = static_cast<std::uint16_t>(dcs % 512);
    }
    return t;
}

// A channel's stored power: 0 low, 1 mid, 2 high, 3 turbo.
PowerLevel powerLevel(unsigned stored)
{
    switch (stored) {
    case 1:
        return PowerLevel::Mid;
    case 2:
        return PowerLevel::High;
    case 3:
        return PowerLevel::Turbo;
    default:
        return PowerLevel::Low;
    }
}

} // namespace

std::optional<Channel> decodeChannel(std::span<const std::uint8_t, 64> r)
{
    const auto rx = bcd8(&r[0]);
    const auto offset = bcd8(&r[4]);
    if (!rx || !offset || *rx == 0) {
        return std::nullopt;
    }
    Channel c;
    c.receiveHz = *rx * 10;
    const std::uint32_t offsetHz = *offset * 10;
    switch ((r[8] >> 6) & 3) {
    case 1:
        c.transmitHz = c.receiveHz + offsetHz;
        break;
    case 2:
        c.transmitHz = c.receiveHz - offsetHz;
        break;
    default:
        c.transmitHz = c.receiveHz;
        break;
    }
    c.mode = static_cast<ChannelMode>(r[8] & 3);
    c.power = powerLevel((r[8] >> 2) & 3);
    c.wide = (r[8] & 0x10) != 0;
    c.receiveOnly = (r[9] & 0x20) != 0;
    const std::uint16_t custom = le16(&r[0x10]);
    c.receiveTone = decodeTone(r[9] & 3, r[0x0B], le16(&r[0x0E]), custom);
    c.transmitTone = decodeTone((r[9] >> 2) & 3, r[0x0A], le16(&r[0x0C]), custom);
    // Squelch modes: carrier, tone, signalling, tone and signalling, tone or
    // signalling. Signalling alone counts as carrier, the others as tone.
    const unsigned squelch = (r[0x19] >> 4) & 7;
    c.squelchMode = squelch == 0 || squelch == 2 || c.receiveTone.kind == Tone::Kind::None ? SquelchMode::Carrier
                                                                                           : SquelchMode::Tone;
    c.dmr.colourCode = r[0x20] & 0x0F;
    c.dmr.slot = (r[0x21] & 1) != 0 ? 2 : 1;
    c.dmr.groupList = r[0x1C];
    // Bit 2 of byte 0x34 excludes the channel from roaming.
    c.roaming = c.digital() && (r[0x34] & 0x04) == 0;
    std::size_t n = 0;
    for (; n < Channel::NameBytes && r[0x23 + n] != 0; ++n) {
        c.name[n] = static_cast<char>(r[0x23 + n]);
    }
    c.name[n] = '\0';
    return c;
}

CodeplugSettings decodeSettings(std::span<const std::uint8_t> b)
{
    CodeplugSettings s;
    if (b.size() < 0xC0) {
        return s;
    }
    auto squelch = [](std::uint8_t v) { return static_cast<std::uint8_t>(v > 5 ? 3 : v); };
    s.squelchA = squelch(b[0x09]);
    s.squelchB = squelch(b[0x0A]);
    s.transmitTimeoutS = static_cast<std::uint16_t>(b[0x04] * 30);
    s.vfoA = b[0x15] != 0;
    s.vfoB = b[0x16] != 0;
    s.zoneA = b[0x1F];
    s.zoneB = b[0x20];
    s.sideBMain = b[0x2C] != 0;
    const std::uint8_t lock = b[0xBE];
    s.keyLock.knob = (lock & 0x01) != 0;
    s.keyLock.keypad = (lock & 0x02) != 0;
    s.keyLock.side = (lock & 0x08) != 0;
    s.keyLock.forced = (lock & 0x10) != 0;
    // Auto key lock follows the menu exit time: 5 s steps from 5 s.
    const unsigned exitSteps = b[0x37] > 11 ? 11 : b[0x37];
    s.keyLock.autoLockMs = b[0x02] != 0 ? (exitSteps + 1) * 5000 : 0;
    s.batterySave = b[0x0B] <= 2 ? b[0x0B] : 1;
    constexpr std::uint16_t BacklightTimes[] = {0, 5, 10, 15, 20, 25, 30, 60, 120, 180, 240, 300, 900, 1800, 2700, 3600};
    s.backlightS = b[0x27] < std::size(BacklightTimes) ? BacklightTimes[b[0x27]] : 0;
    s.voiceEnhancement = b[0x57] == 1;
    s.gps = b[0x28] != 0;
    // The time zone in half hours from UTC-12:00 (0) through UTC (24).
    s.timeZoneMinutes = b[0x30] <= 52 ? static_cast<std::int16_t>((b[0x30] - 24) * 30) : 0;
    // Side keys PF1..PF3: short presses, then long presses.
    for (std::size_t k = 0; k < CodeplugSettings::SideKeys; ++k) {
        s.sideShort[k] = b[0x10 + k] == KeyRoam ? KeyFunction::Roam : KeyFunction::None;
        s.sideLong[k] = b[0x41 + k] == KeyRoam ? KeyFunction::Roam : KeyFunction::None;
    }
    if (b.size() >= 0xE8) {
        RoamingSettings &r = s.roaming;
        r.zone = b[0xDC] < CodeplugSource::MaxRoamingZones ? b[0xDC] : RoamingSettings::NoZone;
        r.automatic = b[0xE7] == 1;
        r.start = b[0xE0] == 0 ? RoamingSettings::Start::Periodic : RoamingSettings::Start::OutOfRange;
        r.periodMinutes = b[0xBA];
        r.quietS = b[0xBF];
    }
    // The repeater range check: interval in 5 s steps from 5 s, tries 0 to 2
    // for 3 to 5 (larger values are taken as the count itself), notice off
    // or on, and how many notices from 1.
    if (b.size() >= 0xEB) {
        RoamingSettings &r = s.roaming;
        r.rangeCheck = b[0xDD] == 1;
        r.checkIntervalS = static_cast<std::uint8_t>((std::min<int>(b[0xDE], 9) + 1) * 5);
        r.checkTries = b[0xDF] <= 2 ? static_cast<std::uint8_t>(b[0xDF] + 3) : std::min<std::uint8_t>(b[0xDF], 10);
        r.outOfRangeNotices = b[0xE5] != 0 ? static_cast<std::uint8_t>(std::min<int>(b[0xEA], 9) + 1) : 0;
    }
    return s;
}

bool D878Uv2Codeplug::copyValid(std::uint32_t block, bool &valid)
{
    for (std::size_t i = 0; i < m_cachedCount; ++i) {
        if (m_cachedBlock[i] == block) {
            valid = m_cachedValid[i];
            return true;
        }
    }
    std::uint8_t m[8];
    if (!readable(m_storage.read(block + MarkerOffset, m))) {
        return false;
    }
    const bool marked = m[4] == 0x55 && m[5] == 0x55 && m[6] == 0xAA && m[7] == 0xAA;
    const bool superseded = m[0] == 0xAA && m[1] == 0xAA && m[2] == 0x55 && m[3] == 0x55;
    valid = marked && !superseded;
    const std::size_t slot = m_cachedCount < Cached ? m_cachedCount++ : block / BlockBytes % Cached;
    m_cachedBlock[slot] = block;
    m_cachedValid[slot] = valid;
    return true;
}

void D878Uv2Codeplug::forget(std::uint32_t block)
{
    for (std::size_t i = 0; i < m_cachedCount; ++i) {
        if (m_cachedBlock[i] == block) {
            m_cachedBlock[i] = m_cachedBlock[m_cachedCount - 1];
            m_cachedValid[i] = m_cachedValid[m_cachedCount - 1];
            --m_cachedCount;
            return;
        }
    }
}

StorageResult D878Uv2Codeplug::read(std::uint32_t address, std::span<std::uint8_t> data)
{
    StorageResult worst = StorageResult::Ok;
    while (!data.empty()) {
        const std::uint32_t block = address - address % BlockBytes;
        const std::size_t n = std::min<std::size_t>(data.size(), block + BlockBytes - address);
        bool firstValid = false;
        if (!copyValid(block, firstValid)) {
            return StorageResult::Timeout;
        }
        const StorageResult r = m_storage.read(firstValid ? address : address + BlockBytes, data.first(n));
        if (!readable(r)) {
            return r;
        }
        if (r != StorageResult::Ok) {
            worst = r;
        }
        address += static_cast<std::uint32_t>(n);
        data = data.subspan(n);
    }
    return worst;
}

void decodeSettingsExtension(std::span<const std::uint8_t> b, CodeplugSettings &s)
{
    // The GPS mode 0..6 is one less than the systems' bits.
    s.gpsSystems = b[0x35] <= 6 ? static_cast<std::uint8_t>(b[0x35] + 1) : 3;
}

std::optional<CodeplugSettings> D878Uv2Codeplug::settings()
{
    std::uint8_t b[SettingsBytes];
    if (!readable(read(Settings, b))) {
        return std::nullopt;
    }
    CodeplugSettings s = decodeSettings(b);
    std::uint8_t e[SettingsExtensionBytes];
    if (readable(read(SettingsExtension, e))) {
        decodeSettingsExtension(e, s);
    }
    return s;
}

bool D878Uv2Codeplug::bitSet(std::uint32_t bitmap, unsigned index)
{
    std::uint8_t byte = 0;
    return readable(read(bitmap + index / 8, std::span(&byte, 1))) && (byte >> (index % 8) & 1) != 0;
}

ChannelLinks channelLinks(std::span<const std::uint8_t, 64> r)
{
    return {le32(&r[0x14]), r[0x18]};
}

std::optional<Channel> D878Uv2Codeplug::readChannel(std::uint32_t address)
{
    std::uint8_t r[ChannelBytes];
    if (!readable(read(address, r))) {
        return std::nullopt;
    }
    const std::span<const std::uint8_t, 64> record(r);
    std::optional<Channel> c = decodeChannel(record);
    if (c && c->digital()) {
        const ChannelLinks links = channelLinks(record);
        c->dmr.contact = contact(links.contact).value_or(dmr::Target{});
        c->dmr.ownId = radioId(links.radioId).value_or(0);
    }
    return c;
}

std::optional<Channel> D878Uv2Codeplug::channel(std::uint16_t index)
{
    if (index >= MaxChannels || !bitSet(ChannelBitmap, index)) {
        return std::nullopt;
    }
    return readChannel(ChannelBanks + (index / ChannelsPerBank) * ChannelBankStride + (index % ChannelsPerBank) * ChannelBytes);
}

std::optional<dmr::Target> D878Uv2Codeplug::contact(std::uint32_t index)
{
    // The contact bitmap is the one inverted: a clear bit marks a contact.
    std::uint8_t used = 0xFF;
    if (index >= MaxContacts || !readable(read(ContactBitmap + index / 8, std::span(&used, 1))) ||
        (used >> (index % 8) & 1) != 0) {
        return std::nullopt;
    }
    const std::uint32_t address =
        ContactBanks + (index / ContactsPerBank) * ContactBankStride + (index % ContactsPerBank) * ContactBytes;
    std::uint8_t r[0x27];
    if (!readable(read(address, r))) {
        return std::nullopt;
    }
    const auto id = bcd8(&r[0x23]);
    if (!id || r[0] > 2) {
        return std::nullopt;
    }
    constexpr dmr::CallType Types[] = {dmr::CallType::Private, dmr::CallType::Group, dmr::CallType::All};
    return dmr::Target{Types[r[0]], *id};
}

bool D878Uv2Codeplug::contactName(std::uint32_t id, bool group, std::span<char, ContactNameBytes + 1> name)
{
    const std::uint32_t key = bcdKey(id);
    // The first entry with this ID.
    std::uint32_t low = 0;
    std::uint32_t high = MaxContacts;
    std::uint8_t e[ContactIdBytes];
    while (low < high) {
        const std::uint32_t mid = (low + high) / 2;
        if (!readable(read(ContactIds + mid * ContactIdBytes, e))) {
            return false;
        }
        if ((le32(e) >> 1) < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    // A private ID and a talkgroup may share the number: take the matching
    // kind.
    for (std::uint32_t i = low; i < low + 2 && i < MaxContacts; ++i) {
        if (!readable(read(ContactIds + i * ContactIdBytes, e)) || (le32(e) >> 1) != key) {
            return false;
        }
        if (((le32(e) & 1) != 0) != group) {
            continue;
        }
        const std::uint32_t index = le32(&e[4]);
        if (index >= MaxContacts) {
            return false;
        }
        char text[ContactNameBytes];
        const std::uint32_t address =
            ContactBanks + (index / ContactsPerBank) * ContactBankStride + (index % ContactsPerBank) * ContactBytes;
        if (!readable(read(address + 1, std::span(reinterpret_cast<std::uint8_t *>(text), sizeof text)))) {
            return false;
        }
        std::size_t n = 0;
        for (; n < ContactNameBytes && text[n] != '\0'; ++n) {
            name[n] = text[n];
        }
        name[n] = '\0';
        return n != 0;
    }
    return false;
}

bool D878Uv2Codeplug::dmrUser(std::uint32_t id, DmrUser &user)
{
    std::uint8_t c[4];
    if (!readable(read(UserCount, c))) {
        return false;
    }
    const std::uint32_t count = le32(c);
    if (count == 0 || count > MaxUsers) {
        return false;
    }
    const std::uint32_t key = bcdKey(id);
    const auto entryAt = [](std::uint32_t i) { return UserIds + (i / UserIdsPerBank) * UserBankStride + (i % UserIdsPerBank) * ContactIdBytes; };
    std::uint32_t low = 0;
    std::uint32_t high = count;
    std::uint8_t e[ContactIdBytes];
    while (low < high) {
        const std::uint32_t mid = (low + high) / 2;
        if (!readable(read(entryAt(mid), e))) {
            return false;
        }
        if ((le32(e) >> 1) < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low >= count || !readable(read(entryAt(low), e)) || (le32(e) >> 1) != key) {
        return false;
    }
    // The record, which may run over into the next bank.
    std::uint8_t r[UserHeaderBytes + ContactNameBytes + 1 + UserCityBytes + 1 + CallsignBytes + 1];
    std::uint32_t at = le32(&e[4]);
    for (std::size_t done = 0; done < sizeof r;) {
        const std::uint32_t inBank = UserRecordBank - at % UserRecordBank;
        const std::size_t n = std::min<std::size_t>(sizeof r - done, inBank);
        if (!readable(read(UserRecords + (at / UserRecordBank) * UserBankStride + at % UserRecordBank, std::span(r + done, n)))) {
            return false;
        }
        done += n;
        at += static_cast<std::uint32_t>(n);
    }
    // Name, city, callsign: each up to its length and zero-terminated.
    std::size_t i = UserHeaderBytes;
    const auto take = [&r, &i](std::span<char> out, std::size_t most) {
        std::size_t n = 0;
        for (; i < sizeof r && r[i] != 0; ++i) {
            if (n < most && n + 1 < out.size()) {
                out[n++] = static_cast<char>(r[i]);
            }
        }
        if (!out.empty()) {
            out[n] = '\0';
        }
        ++i;
    };
    char city[UserCityBytes + 1];
    take(user.name, ContactNameBytes);
    take(city, UserCityBytes);
    take(user.callsign, CallsignBytes);
    return user.name[0] != '\0' || user.callsign[0] != '\0';
}

std::size_t D878Uv2Codeplug::receiveGroups(std::uint8_t list, std::span<std::uint32_t> out)
{
    if (list >= MaxGroupLists || !bitSet(GroupListBitmap, list)) {
        return 0;
    }
    std::uint8_t members[MaxReceiveGroups * 4];
    if (!readable(read(GroupLists + list * GroupListStride, members))) {
        return 0;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < MaxReceiveGroups && n < out.size(); ++i) {
        const std::uint32_t index = le32(&members[i * 4]);
        if (index == 0xFFFFFFFF) {
            continue;
        }
        if (const auto c = contact(index); c && c->type == dmr::CallType::Group) {
            out[n++] = c->id;
        }
    }
    return n;
}

std::optional<std::uint32_t> D878Uv2Codeplug::radioId(std::uint8_t index)
{
    if (index >= MaxRadioIds || !bitSet(RadioIdBitmap, index)) {
        return std::nullopt;
    }
    std::uint8_t r[4];
    if (!readable(read(RadioIds + index * RadioIdBytes, r))) {
        return std::nullopt;
    }
    return bcd8(&r[0]);
}

std::optional<Channel> D878Uv2Codeplug::vfo(bool sideB)
{
    return readChannel(sideB ? VfoB : VfoA);
}

std::size_t D878Uv2Codeplug::zonesInUse(std::span<std::uint8_t> out)
{
    std::uint8_t bitmap[(MaxZones + 7) / 8];
    if (!readable(read(ZoneBitmap, bitmap))) {
        return 0;
    }
    std::size_t n = 0;
    for (unsigned z = 0; z < MaxZones && n < out.size(); ++z) {
        if ((bitmap[z / 8] >> (z % 8) & 1) != 0) {
            out[n++] = static_cast<std::uint8_t>(z);
        }
    }
    return n;
}

bool D878Uv2Codeplug::zoneInUse(std::uint8_t zone)
{
    return zone < MaxZones && bitSet(ZoneBitmap, zone);
}

bool D878Uv2Codeplug::zoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name)
{
    std::uint8_t raw[ZoneNameBytes];
    name[0] = '\0';
    if (zone >= MaxZones || !readable(read(ZoneNames + zone * ZoneNameStride, raw))) {
        return false;
    }
    std::size_t n = 0;
    for (; n < ZoneNameBytes && raw[n] != 0 && raw[n] != 0xFF; ++n) {
        name[n] = static_cast<char>(raw[n]);
    }
    name[n] = '\0';
    return true;
}

std::size_t D878Uv2Codeplug::zoneChannels(std::uint8_t zone, std::span<std::uint16_t> out)
{
    std::uint8_t raw[MaxZoneChannels * 2];
    if (zone >= MaxZones || !readable(read(ZoneChannels + zone * ZoneChannelStride, raw))) {
        return 0;
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < MaxZoneChannels && n < out.size(); ++i) {
        const std::uint16_t c = le16(&raw[2 * i]);
        if (c != NoChannel && c < MaxChannels) {
            out[n++] = c;
        }
    }
    return n;
}

std::uint16_t D878Uv2Codeplug::zoneSelection(std::uint8_t zone, bool sideB)
{
    std::uint8_t raw[2];
    if (zone >= MaxZones || !readable(read(ZoneSelections + (sideB ? ZoneSelectionSideB : 0) + zone * 2u, raw))) {
        return NoChannel;
    }
    return le16(raw);
}

std::size_t D878Uv2Codeplug::roamingMembers(std::uint8_t zone, std::span<std::uint8_t, MaxRoamingSites> out)
{
    std::uint8_t raw[MaxRoamingSites];
    if (zone >= MaxRoamingZones || !bitSet(RoamingZoneBitmap, zone) ||
        !readable(read(RoamingZones + zone * RoamingZoneBytes, raw))) {
        return 0;
    }
    std::size_t n = 0;
    for (const std::uint8_t index : raw) {
        if (index < MaxRoamingRepeaters) {
            out[n++] = index;
        }
    }
    return n;
}

std::optional<RoamingSite> D878Uv2Codeplug::roamingRepeater(unsigned index)
{
    std::uint8_t r[9];
    if (index >= MaxRoamingRepeaters || !bitSet(RoamingRepeaterBitmap, index) ||
        !readable(read(RoamingRepeaters + index * RoamingRepeaterBytes, r))) {
        return std::nullopt;
    }
    const auto rx = bcd8(&r[0]);
    const auto tx = bcd8(&r[4]);
    if (!rx || !tx || *rx == 0 || *tx == 0) {
        return std::nullopt;
    }
    RoamingSite site;
    site.receiveHz = *rx * 10;
    site.transmitHz = *tx * 10;
    site.colourCode = r[8] <= 15 ? r[8] : RoamingSite::ChannelColourCode;
    return site;
}

std::size_t D878Uv2Codeplug::roamingSites(std::uint8_t zone, std::span<RoamingSite> out)
{
    std::uint8_t members[MaxRoamingSites];
    const std::size_t count = roamingMembers(zone, members);
    std::size_t n = 0;
    for (std::size_t i = 0; i < count && n < out.size(); ++i) {
        if (const auto site = roamingRepeater(members[i])) {
            out[n++] = *site;
        }
    }
    return n;
}

bool D878Uv2Codeplug::roamingSiteName(std::uint8_t zone, std::size_t position, std::span<char, SiteNameBytes + 1> name)
{
    name[0] = '\0';
    std::uint8_t members[MaxRoamingSites];
    const std::size_t count = roamingMembers(zone, members);
    // The position counts the repeaters roamingSites() returns.
    std::size_t seen = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!roamingRepeater(members[i])) {
            continue;
        }
        if (seen++ != position) {
            continue;
        }
        std::uint8_t raw[SiteNameBytes];
        if (!readable(read(RoamingRepeaters + members[i] * RoamingRepeaterBytes + RoamingRepeaterName, raw))) {
            return false;
        }
        std::size_t n = 0;
        for (; n < SiteNameBytes && raw[n] != 0 && raw[n] != 0xFF; ++n) {
            name[n] = static_cast<char>(raw[n]);
        }
        name[n] = '\0';
        return true;
    }
    return false;
}

std::size_t D878Uv2Codeplug::roamingZones(std::span<std::uint8_t> out)
{
    std::uint8_t bitmap[MaxRoamingZones / 8];
    if (!readable(read(RoamingZoneBitmap, bitmap))) {
        return 0;
    }
    std::size_t n = 0;
    for (unsigned z = 0; z < MaxRoamingZones && n < out.size(); ++z) {
        if ((bitmap[z / 8] >> (z % 8) & 1) != 0) {
            out[n++] = static_cast<std::uint8_t>(z);
        }
    }
    return n;
}

bool D878Uv2Codeplug::roamingZoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name)
{
    name[0] = '\0';
    std::uint8_t raw[ZoneNameBytes];
    if (zone >= MaxRoamingZones || !bitSet(RoamingZoneBitmap, zone) ||
        !readable(read(RoamingZones + zone * RoamingZoneBytes + RoamingZoneName, raw))) {
        return false;
    }
    std::size_t n = 0;
    for (; n < ZoneNameBytes && raw[n] != 0 && raw[n] != 0xFF; ++n) {
        name[n] = static_cast<char>(raw[n]);
    }
    name[n] = '\0';
    return true;
}

namespace {

// The copy markers in a block's last 8 bytes: superseded in the first
// four, valid in the last four.
constexpr std::uint8_t ValidMark[4] = {0x55, 0x55, 0xAA, 0xAA};
constexpr std::uint8_t SupersededMark[4] = {0xAA, 0xAA, 0x55, 0x55};

} // namespace

bool D878Uv2CodeplugWriter::startBlock(std::uint32_t block)
{
    bool firstValid = false;
    if (!m_codeplug.copyValid(block, firstValid)) {
        return false;
    }
    m_block = block;
    m_source = firstValid ? block : block + BlockBytes;
    m_target = firstValid ? block + BlockBytes : block;
    if (m_storage.eraseBlock(m_target) != StorageResult::Ok) {
        return false;
    }
    m_open = true;
    return loadPage(0);
}

// The buffer starts as page @p page of the valid copy.
bool D878Uv2CodeplugWriter::loadPage(std::uint32_t page)
{
    m_page = page;
    return readable(m_storage.read(m_source + page * PageBytes, m_buffer));
}

bool D878Uv2CodeplugWriter::programPage()
{
    if (m_page == Pages - 1) {
        // The last page carries the markers: this copy valid, not
        // superseded.
        for (std::size_t i = 0; i < 4; ++i) {
            m_buffer[PageBytes - 8 + i] = 0xFF;
            m_buffer[PageBytes - 4 + i] = ValidMark[i];
        }
    }
    return m_storage.program(m_target + m_page * PageBytes, m_buffer) == StorageResult::Ok;
}

bool D878Uv2CodeplugWriter::finishBlock()
{
    m_open = false;
    // The rest of the block from the valid copy, then the old copy marked.
    for (;;) {
        if (!programPage()) {
            return false;
        }
        if (m_page == Pages - 1) {
            break;
        }
        if (!loadPage(m_page + 1)) {
            return false;
        }
    }
    const bool superseded = m_storage.program(m_source + BlockBytes - 8, SupersededMark) == StorageResult::Ok;
    m_codeplug.forget(m_block);
    return superseded;
}

bool D878Uv2CodeplugWriter::allowed(std::uint32_t address, std::size_t size) const
{
    // Codeplugs use the first block of each pair, below its last 8 bytes,
    // which hold the markers; in rising order.
    const std::uint32_t block = address - address % BlockBytes;
    const std::uint32_t offset = address % BlockBytes;
    bool inCodeplug = false;
    for (const Range &r : Ranges) {
        inCodeplug = inCodeplug || (address >= r.first && address < r.end);
    }
    if (size == 0 || !inCodeplug || (block / BlockBytes) % 2 != 0 || offset + size > BlockBytes - 8) {
        return false;
    }
    if (m_open && (block < m_block || (block == m_block && offset / PageBytes < m_page))) {
        return false;
    }
    return true;
}

bool D878Uv2CodeplugWriter::write(std::uint32_t address, std::span<const std::uint8_t> data)
{
    if (m_failed || !allowed(address, data.size())) {
        return false;
    }
    while (!data.empty()) {
        const std::uint32_t block = address - address % BlockBytes;
        const std::uint32_t offset = address % BlockBytes;
        if (m_open && block != m_block && !finishBlock()) {
            m_failed = true;
            return false;
        }
        if (!m_open && !startBlock(block)) {
            m_failed = true;
            return false;
        }
        const std::uint32_t page = offset / PageBytes;
        while (page > m_page) {
            if (!programPage() || !loadPage(m_page + 1)) {
                m_failed = true;
                return false;
            }
        }
        const std::uint32_t column = offset % PageBytes;
        std::size_t n = PageBytes - column;
        if (n > data.size()) {
            n = data.size();
        }
        for (std::size_t i = 0; i < n; ++i) {
            m_buffer[column + i] = data[i];
        }
        m_wrote = true;
        address += static_cast<std::uint32_t>(n);
        data = data.subspan(n);
    }
    return true;
}

bool D878Uv2CodeplugWriter::finish()
{
    const bool ok = !m_failed && (!m_open || finishBlock());
    m_failed = m_failed || !ok;
    m_wrote = false;
    return ok;
}

} // namespace dmm::services
