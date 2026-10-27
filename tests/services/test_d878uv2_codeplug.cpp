// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "../drivers/fake_spi_nand.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_storage.h"

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

using dmm::drivers::storage::SpiNand;
using dmm::services::Channel;
using dmm::services::ChannelMode;
using dmm::services::D878Uv2Codeplug;
using dmm::services::D878Uv2Storage;
using dmm::services::PowerLevel;
using dmm::services::Tone;

namespace {

void noDelay(unsigned) {}

// A channel record: receive 145.600 MHz, -600 kHz, 25 kHz, analog, high
// power, CTCSS 88.5 Hz on transmit, DCS 023 inverted on receive.
std::array<std::uint8_t, 64> sampleRecord()
{
    std::array<std::uint8_t, 64> r{};
    // 14560000 x 10 Hz in BCD
    r[0] = 0x14;
    r[1] = 0x56;
    r[2] = 0x00;
    r[3] = 0x00;
    // 60000 x 10 Hz
    r[4] = 0x00;
    r[5] = 0x06;
    r[6] = 0x00;
    r[7] = 0x00;
    r[8] = 0x00 | (2 << 2) | 0x10 | (2 << 6); // analog, high, wide, minus offset
    r[9] = 2 | (1 << 2);                      // receive DCS, transmit CTCSS
    r[0x0A] = 9;                              // 88.5 Hz
    r[0x0E] = 19;                             // DCS 023 (octal) = 19, inverted: +512
    r[0x0F] = 0x02;
    r[0x19] = 1 << 4; // squelch on the receive tone
    std::memcpy(&r[0x23], "RPT 2M", 6);
    return r;
}

} // namespace

TEST_CASE("a channel record decodes to frequencies, mode, power and tones")
{
    const auto r = sampleRecord();
    const auto c = dmm::services::decodeChannel(r);
    REQUIRE(c.has_value());
    CHECK(c->receiveHz == 145'600'000);
    CHECK(c->transmitHz == 145'000'000);
    CHECK(c->mode == ChannelMode::Analog);
    CHECK(c->analog());
    CHECK(c->power == PowerLevel::High);
    CHECK(c->wide);
    CHECK_FALSE(c->receiveOnly);
    CHECK(c->transmitTone == Tone{Tone::Kind::Ctcss, 885, false});
    CHECK(c->receiveTone == Tone{Tone::Kind::Dcs, 19, true});
    CHECK(c->squelchMode == dmm::services::SquelchMode::Tone);
    CHECK(std::string(c->name) == "RPT 2M");
}

TEST_CASE("the squelch mode follows the codeplug, carrier without a receive tone")
{
    using dmm::services::SquelchMode;
    auto r = sampleRecord();
    const auto mode = [&](unsigned stored) {
        r[0x19] = static_cast<std::uint8_t>(stored << 4);
        return dmm::services::decodeChannel(r)->squelchMode;
    };
    CHECK(mode(0) == SquelchMode::Carrier);
    CHECK(mode(1) == SquelchMode::Tone);
    CHECK(mode(2) == SquelchMode::Carrier); // signalling only
    CHECK(mode(3) == SquelchMode::Tone);
    CHECK(mode(4) == SquelchMode::Tone);
    r[9] = 1 << 2; // no receive tone
    CHECK(mode(1) == SquelchMode::Carrier);
}

TEST_CASE("plus offset, simplex, digital, custom tone and bad records")
{
    auto r = sampleRecord();
    r[8] = static_cast<std::uint8_t>((r[8] & 0x3F) | (1 << 6));
    CHECK(dmm::services::decodeChannel(r)->transmitHz == 146'200'000);
    r[8] = static_cast<std::uint8_t>(r[8] & 0x3F);
    CHECK(dmm::services::decodeChannel(r)->transmitHz == 145'600'000);
    r[8] = static_cast<std::uint8_t>((r[8] & ~3) | 1);
    CHECK(dmm::services::decodeChannel(r)->mode == ChannelMode::Digital);
    CHECK_FALSE(dmm::services::decodeChannel(r)->analog());
    r[0x0A] = 51; // the channel's own tone, 0.1 Hz little-endian
    r[0x10] = 0xD2;
    r[0x11] = 0x04; // 123.4 Hz
    CHECK(dmm::services::decodeChannel(r)->transmitTone == Tone{Tone::Kind::Ctcss, 1234, false});
    r[0] = 0xFF; // erased flash: not BCD
    CHECK_FALSE(dmm::services::decodeChannel(r).has_value());
}

TEST_CASE("settings: squelch, zones, timeout, key lock, battery save and backlight")
{
    std::vector<std::uint8_t> b(0x100, 0);
    b[0x02] = 1; // auto key lock
    b[0x04] = 4; // 120 s
    b[0x09] = 2;
    b[0x0A] = 9; // out of range: default
    b[0x0B] = 2; // battery save, short sleep
    b[0x15] = 1; // side A in VFO
    b[0x1F] = 3;
    b[0x20] = 7;
    b[0x27] = 7; // backlight 1 min
    b[0x2C] = 1;
    b[0x37] = 2;    // menu exit 15 s
    b[0xBE] = 0x0B; // knob, keypad, side
    const auto s = dmm::services::decodeSettings(b);
    CHECK(s.squelchA == 2);
    CHECK(s.squelchB == 3);
    CHECK(s.transmitTimeoutS == 120);
    CHECK(s.vfoA);
    CHECK_FALSE(s.vfoB);
    CHECK(s.zoneA == 3);
    CHECK(s.zoneB == 7);
    CHECK(s.sideBMain);
    CHECK(s.keyLock.knob);
    CHECK(s.keyLock.keypad);
    CHECK(s.keyLock.side);
    CHECK_FALSE(s.keyLock.forced);
    CHECK(s.keyLock.autoLockMs == 15'000);
    CHECK(s.batterySave == 2);
    CHECK(s.backlightS == 60);
    b[0x02] = 0;
    b[0x0B] = 7;  // out of range: on
    b[0x27] = 99; // out of range: always on
    b[0xBE] = 0x10;
    const auto t = dmm::services::decodeSettings(b);
    CHECK(t.batterySave == 1);
    CHECK(t.backlightS == 0);
    CHECK(t.keyLock.autoLockMs == 0);
    CHECK(t.keyLock.forced);
    CHECK_FALSE(t.keyLock.keypad);
}

TEST_CASE("the codeplug reads channels and zones from the valid copy of each block")
{
    FakeSpiNand chip;
    const std::uint32_t ValidMarker = 0x1FFF8;
    auto markValid = [&](std::uint32_t block) {
        chip.put(block + ValidMarker, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    };
    // Channel bitmap and zone bitmap block: first copy valid.
    markValid(0x024C0000);
    chip.put(0x024C1500, {0x05});                 // channels 0 and 2 in use
    std::vector<std::uint8_t> zoneBits(32, 0x00); // the codeplug writes the whole bitmap
    zoneBits[0] = 0x01;                           // zone 0 in use
    chip.put(0x024C1300, zoneBits);
    // Channel bank 0: the first copy is stale, the second holds the data.
    const auto rec = sampleRecord();
    std::vector<std::uint8_t> two(rec.begin(), rec.end());
    two[0x23] = 'B';
    chip.put(0x00800000 + 2 * 0x40, {0xFF}); // garbage in the stale copy
    chip.put(0x00820000 + 2 * 0x40, two);
    chip.put(0x00820000, std::vector<std::uint8_t>(rec.begin(), rec.end()));
    markValid(0x00820000); // the first copy (0x00800000) has no marker
    // Zone 0: channels 2 then 0; name; selections.
    markValid(0x01000000);
    chip.put(0x01000000, {0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF});
    markValid(0x02540000);
    chip.put(0x02540000, {'R', 'e', 'p', 'e', 'a', 't', 'e', 'r', 's'});
    markValid(0x02500000);
    chip.put(0x02500000 + 0x09, {4});
    chip.put(0x02500100, {0x02, 0x00});

    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();
    D878Uv2Codeplug cp(storage);

    const auto c0 = cp.channel(0);
    REQUIRE(c0.has_value());
    CHECK(c0->receiveHz == 145'600'000);
    const auto c2 = cp.channel(2);
    REQUIRE(c2.has_value());
    CHECK(c2->name[0] == 'B');
    CHECK_FALSE(cp.channel(1).has_value()); // not in the bitmap
    CHECK_FALSE(cp.channel(4000).has_value());

    CHECK(cp.zoneInUse(0));
    CHECK_FALSE(cp.zoneInUse(1));
    std::array<std::uint8_t, 4> used{};
    REQUIRE(cp.zonesInUse(used) == 1);
    CHECK(used[0] == 0);
    std::array<char, D878Uv2Codeplug::ZoneNameBytes + 1> name{};
    REQUIRE(cp.zoneName(0, name));
    CHECK(std::string(name.data()) == "Repeaters");
    std::array<std::uint16_t, 8> list{};
    REQUIRE(cp.zoneChannels(0, list) == 2);
    CHECK(list[0] == 2);
    CHECK(list[1] == 0);
    CHECK(cp.zoneSelection(0, false) == 2);
    const auto s = cp.settings();
    REQUIRE(s.has_value());
    CHECK(s->squelchA == 4);
}

TEST_CASE("a digital channel's colour code, slot, group list and links")
{
    auto r = sampleRecord();
    r[8] = static_cast<std::uint8_t>((r[8] & ~3) | 1); // digital
    r[0x14] = 0x34;                                    // contact 0x1234, little-endian
    r[0x15] = 0x12;
    r[0x18] = 2;    // radio ID 2
    r[0x1C] = 5;    // receive group list 5
    r[0x20] = 0x07; // colour code 7
    r[0x21] = 0x01; // slot 2
    const auto c = dmm::services::decodeChannel(r);
    REQUIRE(c.has_value());
    CHECK(c->digital());
    CHECK(c->dmr.colourCode == 7);
    CHECK(c->dmr.slot == 2);
    CHECK(c->dmr.groupList == 5);
    const auto links = dmm::services::channelLinks(r);
    CHECK(links.contact == 0x1234);
    CHECK(links.radioId == 2);
    r[0x21] = 0x00;
    r[0x1C] = 0xFF;
    CHECK(dmm::services::decodeChannel(r)->dmr.slot == 1);
    CHECK(dmm::services::decodeChannel(r)->dmr.groupList == dmm::services::dmr::ChannelSettings::NoGroupList);
}

TEST_CASE("the codeplug looks up a digital channel's contact and own ID")
{
    using dmm::services::dmr::CallType;
    using dmm::services::dmr::Target;
    FakeSpiNand chip;
    auto markValid = [&](std::uint32_t block) {
        chip.put(block + 0x1FFF8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    };
    markValid(0x024C0000);
    chip.put(0x024C1500, {0x01}); // channel 0 in use
    chip.put(0x024C1320, {0x04}); // radio ID 2 in use
    markValid(0x02640000);
    chip.put(0x02640000 + 125, {0x02}); // contact 1001 in use
    // Channel 0: digital, contact 1001, radio ID 2, colour code 3, slot 1.
    auto rec = sampleRecord();
    rec[8] = static_cast<std::uint8_t>((rec[8] & ~3) | 1);
    rec[0x14] = 0xE9;
    rec[0x15] = 0x03;
    rec[0x18] = 2;
    rec[0x20] = 3;
    markValid(0x00800000);
    chip.put(0x00800000, std::vector<std::uint8_t>(rec.begin(), rec.end()));
    // Contact 1001: second bank, entry 1: a group call to talkgroup 2441.
    markValid(0x026C0000);
    std::vector<std::uint8_t> contact(0x27, 0);
    contact[0] = 1;
    contact[0x23] = 0x00;
    contact[0x24] = 0x00;
    contact[0x25] = 0x24;
    contact[0x26] = 0x41;
    chip.put(0x026C0000 + 0x64, contact);
    // Radio ID 2: 7654321.
    markValid(0x02580000);
    chip.put(0x02580000 + 2 * 0x20, {0x07, 0x65, 0x43, 0x21});

    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();
    D878Uv2Codeplug cp(storage);

    const auto c = cp.channel(0);
    REQUIRE(c.has_value());
    CHECK(c->dmr.colourCode == 3);
    CHECK(c->dmr.contact == Target{CallType::Group, 2441});
    CHECK(c->dmr.ownId == 7654321);
    CHECK_FALSE(cp.contact(1000).has_value()); // not in the bitmap
    CHECK_FALSE(cp.radioId(1).has_value());
}

TEST_CASE("contact names are found by ID through the sorted ID table")
{
    FakeSpiNand chip;
    auto markValid = [&](std::uint32_t block) {
        chip.put(block + 0x1FFF8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
    };
    // Sorted IDs: private 244 (index 2), talkgroup 244 (index 0), private
    // 1234567 (index 1); the rest erased.
    auto entry = [](std::uint32_t bcd, bool group, std::uint32_t index) {
        const std::uint32_t v = bcd << 1 | (group ? 1u : 0u);
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
            static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(index),
            0, 0, 0};
    };
    std::vector<std::uint8_t> table;
    for (const auto &e : {entry(0x244, false, 2), entry(0x244, true, 0), entry(0x1234567, false, 1)}) {
        table.insert(table.end(), e.begin(), e.end());
    }
    table.resize(0x2000, 0xFF);
    markValid(0x04340000);
    chip.put(0x04340000, table);
    markValid(0x02680000);
    auto named = [&](std::uint32_t index, const char *name) {
        std::vector<std::uint8_t> r(0x10, 0);
        std::memcpy(&r[1], name, std::strlen(name));
        chip.put(0x02680000 + index * 0x64, r);
    };
    named(0, "Suomi");
    named(1, "Caller One");
    named(2, "Somebody");

    SpiNand nand(chip, noDelay);
    D878Uv2Storage storage(nand);
    storage.start();
    D878Uv2Codeplug cp(storage);
    std::array<char, D878Uv2Codeplug::ContactNameBytes + 1> name{};
    REQUIRE(cp.contactName(244, true, name));
    CHECK(std::string(name.data()) == "Suomi");
    REQUIRE(cp.contactName(244, false, name));
    CHECK(std::string(name.data()) == "Somebody");
    REQUIRE(cp.contactName(1234567, false, name));
    CHECK(std::string(name.data()) == "Caller One");
    CHECK_FALSE(cp.contactName(1234568, false, name));
    CHECK_FALSE(cp.contactName(1, true, name));
}
