// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "boot_test_rig.h"

#include "dmm/services/boot_record.h"

#include <doctest/doctest.h>

using namespace boot_test;

namespace {

BootState sample()
{
    BootState s;
    s.a = {SlotState::Trial, ConfirmMode::Manual, 12345, 0xCAFEF00D};
    s.b = {SlotState::Confirmed, ConfirmMode::Automatic, 9000, 0x12345678};
    s.attempts = 2;
    s.lastFallback = Fallback::CheckFailed;
    return s;
}

} // namespace

TEST_CASE("boot record: an erased record is not valid and takes nothing")
{
    Rig rig;
    BootRecord record(rig.guarded, Small.record);
    CHECK_FALSE(record.load());
    CHECK_FALSE(record.append(entry::attempt(1)));
    CHECK(record.state() == BootState{});
}

TEST_CASE("boot record: rewrite, append and reload give the same state")
{
    Rig rig;
    BootRecord record(rig.guarded, Small.record);
    REQUIRE(record.rewrite(sample()));
    CHECK(record.state() == sample());
    REQUIRE(record.append(entry::attempt(3)));
    REQUIRE(record.append(entry::confirm(0xCAFEF00D)));
    BootRecord again(rig.guarded, Small.record);
    REQUIRE(again.load());
    CHECK(again.state().attempts == 3);
    CHECK(again.state().a.state == SlotState::Confirmed);
    CHECK(again.state() == record.state());
}

TEST_CASE("boot record: a confirmation for another image changes nothing")
{
    BootState s = sample();
    apply(s, entry::confirm(0x1111));
    CHECK(s.a.state == SlotState::Trial);
}

TEST_CASE("boot record: entry checks are never erased or zero words")
{
    for (std::uint32_t w = 0; w < 5000; ++w) {
        const std::uint32_t c = entryCheck(w * 0x01000193u, w, ~w);
        REQUIRE(c != 0);
        REQUIRE(c != 0xFFFFFFFFu);
    }
}

TEST_CASE("boot record: a torn or cancelled entry is skipped, later ones count")
{
    Rig rig;
    BootRecord record(rig.guarded, Small.record);
    REQUIRE(record.rewrite(sample()));
    REQUIRE(record.append(entry::attempt(1)));
    // Tear the attempt: one bit of its check word never came down.
    const std::uint32_t torn = Small.record[0] + 5 * BootRecord::EntryBytes;
    rig.raw.at(torn + 12)[0] ^= 0x01;
    REQUIRE(record.append(entry::fallback(Fallback::Requested)));
    // Cancel the fallback entry with zeros.
    const std::uint8_t zeros[16] = {};
    REQUIRE(rig.guarded.program(Small.record[0] + 6 * BootRecord::EntryBytes, zeros));
    REQUIRE(record.append(entry::attempt(3)));

    BootRecord again(rig.guarded, Small.record);
    REQUIRE(again.load());
    CHECK(again.state().attempts == 3);
    CHECK(again.state().lastFallback == Fallback::CheckFailed);
}

TEST_CASE("boot record: a full sector refuses entries; reserve rewrites into the other")
{
    Rig rig;
    BootRecord record(rig.guarded, Small.record);
    REQUIRE(record.rewrite(sample()));
    const std::uint32_t firstGeneration = record.generation();
    while (record.freeEntries() > 0) {
        REQUIRE(record.append(entry::attempt(static_cast<std::uint8_t>(record.freeEntries() % 7))));
    }
    const BootState full = record.state();
    CHECK_FALSE(record.append(entry::attempt(1)));
    REQUIRE(record.reserve(10));
    CHECK(record.generation() == firstGeneration + 1);
    CHECK(record.freeEntries() >= 10);
    CHECK(record.state() == full);
    // And back into the first sector the next time.
    while (record.freeEntries() > 0) {
        REQUIRE(record.append(entry::fallback(Fallback::NoImage)));
    }
    REQUIRE(record.reserve(1));
    CHECK(record.generation() == firstGeneration + 2);
    BootRecord again(rig.guarded, Small.record);
    REQUIRE(again.load());
    CHECK(again.state() == record.state());
    CHECK(again.generation() == firstGeneration + 2);
}

TEST_CASE("boot record: a rewrite cut at any point leaves the old or the new state")
{
    for (long k = 0; k < 8; ++k) {
        CAPTURE(k);
        Rig rig;
        BootRecord record(rig.guarded, Small.record);
        REQUIRE(record.rewrite(sample()));
        REQUIRE(record.append(entry::attempt(3)));
        const BootState before = record.state();
        BootState after = before;
        after.attempts = 0;
        after.lastFallback = Fallback::Requested;
        rig.raw.powerOn();
        rig.raw.cutAt(k);
        const bool done = record.rewrite(after);
        rig.raw.powerOn();
        BootRecord again(rig.guarded, Small.record);
        REQUIRE(again.load());
        if (done) {
            CHECK(again.state() == after);
        } else {
            CHECK((again.state() == before || again.state() == after));
        }
    }
}

TEST_CASE("boot record: an append cut at any point leaves the old or the new state")
{
    for (long k = 0; k < 2; ++k) {
        Rig rig;
        BootRecord record(rig.guarded, Small.record);
        REQUIRE(record.rewrite(sample()));
        rig.raw.powerOn();
        rig.raw.cutAt(k);
        record.append(entry::confirm(0xCAFEF00D));
        rig.raw.powerOn();
        BootRecord again(rig.guarded, Small.record);
        REQUIRE(again.load());
        const SlotState a = again.state().a.state;
        CHECK((a == SlotState::Trial || a == SlotState::Confirmed));
        // Appending goes on after the torn slot.
        REQUIRE(again.append(entry::confirm(0xCAFEF00D)));
        BootRecord third(rig.guarded, Small.record);
        REQUIRE(third.load());
        CHECK(third.state().a.state == SlotState::Confirmed);
    }
}

TEST_CASE("boot record: the swap's end exchanges the slots as its kind says")
{
    BootState s;
    s.a = {SlotState::Confirmed, ConfirmMode::Automatic, 100, 1};
    s.b = {SlotState::Committed, ConfirmMode::Manual, 200, 2};
    s.attempts = 0;
    apply(s, entry::swapBegin(SwapKind::Install, Fallback::None, 3));
    apply(s, entry::swapStep(0));
    CHECK(s.swap.done == 1);
    apply(s, entry::swapEnd());
    CHECK(s.a == SlotRecord{SlotState::Trial, ConfirmMode::Manual, 200, 2});
    CHECK(s.b == SlotRecord{SlotState::Confirmed, ConfirmMode::Automatic, 100, 1});
    CHECK_FALSE(s.swap.active);

    apply(s, entry::attempt(3));
    apply(s, entry::swapBegin(SwapKind::Revert, Fallback::AttemptsUsed, 3));
    apply(s, entry::swapEnd());
    CHECK(s.a.state == SlotState::Confirmed);
    CHECK(s.a.length == 100);
    CHECK(s.b.state == SlotState::Failed);
    CHECK(s.b.length == 200);
    CHECK(s.attempts == 0);
    CHECK(s.lastFallback == Fallback::AttemptsUsed);

    // A rollback between two confirmed images keeps both confirmed.
    s.b.state = SlotState::Confirmed;
    apply(s, entry::swapBegin(SwapKind::Rollback, Fallback::Requested, 3));
    apply(s, entry::swapEnd());
    CHECK(s.a == SlotRecord{SlotState::Confirmed, ConfirmMode::Manual, 200, 2});
    CHECK(s.b == SlotRecord{SlotState::Confirmed, ConfirmMode::Automatic, 100, 1});
    CHECK(s.lastFallback == Fallback::Requested);

    // A stray end without a beginning changes nothing.
    const BootState copy = s;
    apply(s, entry::swapEnd());
    CHECK(s == copy);
}
