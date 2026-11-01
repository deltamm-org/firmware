// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "../drivers/fake_spi_nand.h"
#include "dmm/lib/crc32.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "dmm/services/d878uv2_preferences.h"
#include "dmm/services/d878uv2_settings_store.h"
#include "dmm/services/d878uv2_storage.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

using dmm::drivers::storage::SpiNand;
using dmm::services::CodeplugSettings;
using dmm::services::D878Uv2Codeplug;
using dmm::services::D878Uv2CodeplugWriter;
using dmm::services::D878Uv2SettingsStore;
using dmm::services::D878Uv2Storage;
using dmm::services::PowerLevel;
using dmm::services::RadioPreferences;
using dmm::services::SaveResult;
using dmm::services::SquelchMode;
using dmm::services::d878uv2::PreferencesRecordBytes;
using Record = std::array<std::uint8_t, PreferencesRecordBytes>;

namespace {

void noDelay(unsigned) {}

constexpr std::uint32_t BlockBytes = 0x20000;
constexpr std::uint32_t Settings = 0x02500000;
constexpr std::uint32_t ZoneSelections = Settings + 0x100;
constexpr std::uint32_t GpsSystems = Settings + 0x1435;
constexpr std::uint32_t Channels = 0x00800000;
constexpr std::uint32_t Preferences = 0x0081E000;

// A codeplug with general settings and two channels; the store writes
// through the codeplug writer, as on the radio.
struct Radio
{
    FakeSpiNand chip;
    SpiNand nand{chip, noDelay};
    D878Uv2Storage storage{nand};
    D878Uv2Codeplug codeplug{storage};
    D878Uv2Codeplug writerCodeplug{storage};
    D878Uv2CodeplugWriter writer{storage, writerCodeplug};
    // The addresses of each write's pieces.
    std::vector<std::vector<std::uint32_t>> writes;
    SaveResult answer = SaveResult::Saved;
    std::function<SaveResult(std::span<const D878Uv2SettingsStore::Piece>)> writeFn =
        [this](std::span<const D878Uv2SettingsStore::Piece> pieces) {
            if (answer != SaveResult::Saved) {
                return answer;
            }
            std::vector<std::uint32_t> addresses;
            bool ok = true;
            for (const auto &p : pieces) {
                addresses.push_back(p.address);
                ok = ok && writer.write(p.address, p.data);
            }
            ok = ok && writer.finish();
            if (!ok) {
                writer.abandon();
            }
            writes.push_back(addresses);
            codeplug.forgetCopies();
            return ok ? SaveResult::Saved : SaveResult::Failed;
        };
    D878Uv2SettingsStore store{codeplug, writeFn};

    Radio()
    {
        auto markValid = [&](std::uint32_t block) {
            chip.put(block + BlockBytes - 8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
        };
        markValid(Settings);
        std::vector<std::uint8_t> s(0x200, 0);
        s[0x09] = 1;
        s[0x0A] = 1;
        s[0x26] = 3; // brightness 4
        s[0x30] = 24;
        s[0xBE] = 0x40; // a bit the store leaves alone
        s[0xDC] = 0;
        chip.put(Settings, s);
        chip.put(Settings + 0x1400, std::vector<std::uint8_t>(0x40, 0));
        markValid(Channels);
        std::vector<std::uint8_t> ch(0x80, 0);
        for (int i = 0; i < 2; ++i) {
            ch[i * 0x40 + 0] = 0x43; // 433.50000 MHz
            ch[i * 0x40 + 1] = 0x35;
            ch[i * 0x40 + 8] = 0x01 | 0x40; // digital, low power, a bit to keep
            ch[i * 0x40 + 9] = 1;           // receive CTCSS
            ch[i * 0x40 + 0x0B] = 9;
            ch[i * 0x40 + 0x19] = 0x80; // carrier squelch, a bit to keep
        }
        chip.put(Channels, ch);
        storage.start();
    }

    std::uint8_t byte(std::uint32_t address)
    {
        std::uint8_t b = 0;
        REQUIRE(dmm::services::readable(codeplug.read(address, std::span(&b, 1))));
        return b;
    }
};

// Redoes @p r's checksum after a change.
void seal(Record &r)
{
    const std::uint32_t crc = dmm::lib::crc32(r.data(), r.size() - 4);
    for (std::size_t i = 0; i < 4; ++i) {
        r[r.size() - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
}

Record encoded(const RadioPreferences &p)
{
    Record r{};
    dmm::services::d878uv2::encodePreferences(p, r);
    return r;
}

} // namespace

TEST_CASE("settings store: kept settings are written after a quiet time, in their places")
{
    Radio r;
    CodeplugSettings s = *r.codeplug.settings();
    s.squelchA = 4;
    s.backlightLevel = 2;
    s.backlightS = 60;
    s.gps = true;
    s.gpsSystems = 7;
    s.timeZoneMinutes = 180;
    s.batterySave = 1;
    s.keyLock.knob = true;
    s.keyLock.autoLockMs = 30'000;
    s.roaming.zone = 2;
    r.store.keepSettings(s, 1000);
    CHECK(r.store.pending());
    CHECK(r.store.flush(1000 + D878Uv2SettingsStore::QuietMs - 1) == SaveResult::Unchanged);
    CHECK(r.writes.empty());
    CHECK(r.store.flush(1000 + D878Uv2SettingsStore::QuietMs) == SaveResult::Saved);
    CHECK_FALSE(r.store.pending());
    REQUIRE(r.writes.size() == 1);
    CHECK(r.writes[0] == std::vector<std::uint32_t>{Settings, GpsSystems});

    const CodeplugSettings t = *r.codeplug.settings();
    CHECK(t.squelchA == 4);
    CHECK(t.squelchB == 1);
    CHECK(t.backlightLevel == 2);
    CHECK(t.backlightS == 60);
    CHECK(t.gps);
    CHECK(t.gpsSystems == 7);
    CHECK(t.timeZoneMinutes == 180);
    CHECK(t.batterySave == 1);
    CHECK(t.keyLock.knob);
    CHECK(t.keyLock.autoLockMs == 30'000);
    CHECK(t.roaming.zone == 2);
    CHECK((r.byte(Settings + 0xBE) & 0x40) != 0);
}

TEST_CASE("settings store: a change restarts the quiet time; an unchanged value writes nothing")
{
    Radio r;
    CodeplugSettings s = *r.codeplug.settings();
    r.store.keepSettings(s, 0);
    s.squelchB = 5;
    r.store.keepSettings(s, 2000);
    CHECK(r.store.flush(D878Uv2SettingsStore::QuietMs) == SaveResult::Unchanged);
    CHECK(r.store.pending());
    CHECK(r.store.flush(2000 + D878Uv2SettingsStore::QuietMs) == SaveResult::Saved);
    CHECK(r.writes.size() == 1);

    r.store.keepSettings(*r.codeplug.settings(), 10'000);
    CHECK(r.store.flush(10'000 + D878Uv2SettingsStore::QuietMs) == SaveResult::Unchanged);
    CHECK(r.writes.size() == 1);
    CHECK_FALSE(r.store.pending());
}

TEST_CASE("settings store: zone, channel and preferences go out in one write, in address order")
{
    Radio r;
    RadioPreferences p;
    p.roaming = false;
    CodeplugSettings s = *r.codeplug.settings();
    s.squelchA = 2;
    // Kept in another order than the addresses.
    r.store.keepZone(true, 3, 5, 0);
    r.store.keepSettings(s, 0);
    r.store.keepPreferences(p, 0);
    r.store.keepChannel(1, PowerLevel::High, SquelchMode::Tone, 0);
    r.store.keepZone(false, 4, 2, 0);
    CHECK(r.store.flush(D878Uv2SettingsStore::QuietMs) == SaveResult::Saved);
    REQUIRE(r.writes.size() == 1);
    CHECK(r.writes[0] ==
        std::vector<std::uint32_t>{Channels + 0x40, Preferences, Settings, ZoneSelections + 4 * 2,
            ZoneSelections + 0x200 + 3 * 2});

    const CodeplugSettings t = *r.codeplug.settings();
    CHECK(t.zoneA == 4);
    CHECK(t.zoneB == 3);
    CHECK(t.squelchA == 2);
    CHECK(r.codeplug.zoneSelection(3, true) == 5);
    CHECK(r.codeplug.zoneSelection(4, false) == 2);
    const auto c = r.codeplug.channel(1);
    REQUIRE(c.has_value());
    CHECK(c->power == PowerLevel::High);
    CHECK(c->squelchMode == SquelchMode::Tone);
    CHECK((r.byte(Channels + 0x48) & 0x40) != 0);
    CHECK((r.byte(Channels + 0x59) & 0x80) != 0);
    CHECK(r.codeplug.channel(0)->power == PowerLevel::Low);
    const auto q = r.codeplug.preferences();
    REQUIRE(q.has_value());
    CHECK_FALSE(q->roaming);
}

TEST_CASE("settings store: a change on another channel writes the first one's at once")
{
    Radio r;
    r.store.keepChannel(0, PowerLevel::Mid, SquelchMode::Carrier, 0);
    r.store.keepChannel(0, PowerLevel::High, SquelchMode::Carrier, 10);
    CHECK(r.writes.empty());
    r.store.keepChannel(1, PowerLevel::Turbo, SquelchMode::Carrier, 20);
    REQUIRE(r.writes.size() == 1);
    CHECK(r.codeplug.channel(0)->power == PowerLevel::High);
    CHECK(r.store.pending());
    CHECK(r.store.flush(20 + D878Uv2SettingsStore::QuietMs) == SaveResult::Saved);
    CHECK(r.codeplug.channel(1)->power == PowerLevel::Turbo);
}

TEST_CASE("settings store: a write refused while the PC programs is tried again later")
{
    Radio r;
    CodeplugSettings s = *r.codeplug.settings();
    s.squelchA = 5;
    r.store.keepSettings(s, 0);
    r.answer = SaveResult::Busy;
    for (unsigned i = 1; i <= D878Uv2SettingsStore::MaxFailures + 2; ++i) {
        CHECK(r.store.flush(i * D878Uv2SettingsStore::QuietMs) == SaveResult::Busy);
    }
    CHECK(r.store.pending());
    r.answer = SaveResult::Saved;
    const std::uint32_t last = (D878Uv2SettingsStore::MaxFailures + 2) * D878Uv2SettingsStore::QuietMs;
    CHECK(r.store.flush(last + 1) == SaveResult::Unchanged);
    CHECK(r.store.flush(last + D878Uv2SettingsStore::QuietMs) == SaveResult::Saved);
    CHECK(r.codeplug.settings()->squelchA == 5);
}

TEST_CASE("settings store: failed writes are retried, then dropped")
{
    Radio r;
    CodeplugSettings s = *r.codeplug.settings();
    s.squelchA = 5;
    r.store.keepSettings(s, 0);
    r.answer = SaveResult::Failed;
    std::uint32_t now = 0;
    for (unsigned i = 0; i < D878Uv2SettingsStore::MaxFailures; ++i) {
        now += D878Uv2SettingsStore::QuietMs;
        CHECK(r.store.flush(now) == SaveResult::Failed);
    }
    CHECK_FALSE(r.store.pending());
    r.answer = SaveResult::Saved;
    CHECK(r.store.flush(now + D878Uv2SettingsStore::QuietMs) == SaveResult::Unchanged);
    CHECK(r.codeplug.settings()->squelchA == 1);
}

TEST_CASE("preferences record: round trip, and none in an erased codeplug")
{
    Radio r;
    CHECK_FALSE(r.codeplug.preferences().has_value());
    for (const bool roaming : {false, true}) {
        RadioPreferences p;
        p.roaming = roaming;
        const auto q = dmm::services::d878uv2::decodePreferences(encoded(p));
        REQUIRE(q.has_value());
        CHECK(q->roaming == roaming);
    }
}

TEST_CASE("preferences record: a damaged record, or another mark or version, counts as none")
{
    RadioPreferences p;
    p.roaming = false;
    Record r = encoded(p);
    r[6] ^= 1;
    CHECK_FALSE(dmm::services::d878uv2::decodePreferences(r).has_value());

    r = encoded(p);
    r[0] = 'X';
    seal(r);
    CHECK_FALSE(dmm::services::d878uv2::decodePreferences(r).has_value());

    r = encoded(p);
    r[4] = 2;
    seal(r);
    CHECK_FALSE(dmm::services::d878uv2::decodePreferences(r).has_value());

    r = encoded(p);
    r[5] = static_cast<std::uint8_t>(dmm::services::d878uv2::PreferencesFieldBytes + 1);
    seal(r);
    CHECK_FALSE(dmm::services::d878uv2::decodePreferences(r).has_value());
}

TEST_CASE("preferences record: fields a shorter record lacks, or out of range, read as defaults")
{
    RadioPreferences p;
    p.roaming = false;
    Record r = encoded(p);
    // A record from before the field: fewer field bytes.
    r[5] = 0;
    seal(r);
    auto q = dmm::services::d878uv2::decodePreferences(r);
    REQUIRE(q.has_value());
    CHECK(q->roaming == RadioPreferences{}.roaming);

    r = encoded(p);
    r[6] = 7;
    seal(r);
    q = dmm::services::d878uv2::decodePreferences(r);
    REQUIRE(q.has_value());
    CHECK(q->roaming == RadioPreferences{}.roaming);

    // A record from a later version with more fields keeps the known ones.
    r = encoded(p);
    r[5] = 20;
    r[7] = 0x12;
    seal(r);
    q = dmm::services::d878uv2::decodePreferences(r);
    REQUIRE(q.has_value());
    CHECK_FALSE(q->roaming);
}
