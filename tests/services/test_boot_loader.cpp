// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "boot_test_rig.h"

#include "dmm/services/boot_loader.h"
#include "dmm/services/d878uv2_boot_layout.h"

#include <doctest/doctest.h>

#include <functional>

using namespace boot_test;

namespace {

const std::vector<std::uint8_t> &oldImage()
{
    static const auto image = makeImage(Small, 3 * SectorBytes + 100, 1, "old");
    return image;
}

const std::vector<std::uint8_t> &newImage()
{
    static const auto image = makeImage(Small, 4 * SectorBytes - 8, 2, "new");
    return image;
}

/// A radio with the old image adopted and running.
void installOld(Rig &rig)
{
    rig.putInSlot(false, oldImage());
    REQUIRE(rig.settle().outcome == Outcome::StartImage);
    REQUIRE(rig.state().a == SlotRecord{SlotState::Confirmed, ConfirmMode::Automatic, static_cast<std::uint32_t>(oldImage().size()), crcOf(oldImage())});
}

/// The image in slot A passes its full check against the record.
bool runsGoodImage(Rig &rig)
{
    const BootState s = rig.state();
    return (s.a.state == SlotState::Confirmed || s.a.state == SlotState::Trial) &&
        checkImage(rig.guarded, rig.layout, rig.layout.slotA.begin, s.a.length, true, s.a.crc) == ImageProblem::None;
}

/// Runs @p scenario once uncut to count its flash operations, then once
/// for every cut point; after each cut @p check sees the radio started
/// again until it settled.
void everyCut(const std::function<void(Rig &)> &setup, const std::function<void(Rig &)> &scenario,
    const std::function<void(Rig &, long)> &check, bool twoCuts = false)
{
    long total = 0;
    {
        Rig rig;
        setup(rig);
        rig.raw.powerOn();
        scenario(rig);
        total = rig.raw.operations();
        REQUIRE(total > 0);
    }
    for (long k = 0; k < total; ++k) {
        long recoveryOps = 1;
        for (long k2 = -1; k2 < recoveryOps; ++k2) {
            CAPTURE(k);
            CAPTURE(k2);
            Rig rig;
            setup(rig);
            rig.raw.powerOn();
            rig.raw.cutAt(k);
            scenario(rig);
            REQUIRE_FALSE(rig.raw.powered());
            rig.raw.powerOn();
            if (k2 >= 0) {
                rig.raw.cutAt(k2);
                rig.settle();
                rig.raw.powerOn();
            }
            const BootResult r = rig.settle();
            if (k2 < 0) {
                recoveryOps = twoCuts ? rig.raw.operations() : 0;
            }
            CHECK(r.outcome == Outcome::StartImage);
            CHECK(runsGoodImage(rig));
            CHECK(rig.untouched());
            check(rig, k);
        }
    }
}

} // namespace

TEST_CASE("guard: erases and programs only inside the allowed areas")
{
    FakeProgramFlash raw(0x08000000, 0x100000, 0x08080000);
    namespace d = dmm::services::d878uv2;
    GuardedFlash guard(raw, d::LoaderWritable);
    const std::uint8_t word[4] = {1, 2, 3, 4};
    // Allowed edges.
    CHECK(guard.eraseSector(d::SlotABase));
    CHECK(guard.eraseSector(d::SlotBEnd - SectorBytes));
    CHECK(guard.eraseSector(d::RecordEnd - SectorBytes));
    CHECK(guard.program(d::SlotABase, word));
    CHECK(guard.program(d::RecordEnd - 4, word));
    CHECK(guard.refused() == 0);
    // Everything else, edges first.
    for (std::uint32_t at : {d::StubBase, d::SlotABase - SectorBytes, d::RecordEnd, d::BasebandAreaBase, d::LoaderBase,
             d::LoaderEnd - SectorBytes, d::LoaderEnd, 0x080F5000u, 0x08000000u, 0x080FF000u, 0x08100000u}) {
        CAPTURE(at);
        CHECK_FALSE(guard.eraseSector(at));
    }
    CHECK_FALSE(guard.eraseSector(d::SlotABase + 2048));
    CHECK_FALSE(guard.program(d::SlotABase - 4, word));
    CHECK_FALSE(guard.program(d::RecordEnd, word));
    CHECK_FALSE(guard.program(d::LoaderBase, word));
    CHECK_FALSE(guard.program(d::RadioDataBase, word));
    CHECK_FALSE(guard.program(d::StubEnd - 4, word));
    CHECK_FALSE(guard.program(0x08003FFCu, word));
    CHECK_FALSE(guard.program(d::SlotABase + 2, word));
    // A range that starts inside and runs out.
    const std::uint8_t eight[8] = {};
    CHECK_FALSE(guard.program(d::RecordEnd - 4, eight));
    CHECK_FALSE(guard.program(0xFFFFFFFCu, eight));
    CHECK(guard.refused() == 21);
    // The radio's own areas never changed.
    for (std::uint32_t at = 0x08000000; at < d::SlotABase; ++at) {
        REQUIRE(*raw.at(at) == 0xFF);
    }

    GuardedFlash imageGuard(raw, d::ImageWritable);
    CHECK_FALSE(imageGuard.eraseSector(d::SlotABase));
    CHECK(imageGuard.program(d::RecordBase + 16, word));
    CHECK_FALSE(imageGuard.program(d::SlotBBase, word));
}

TEST_CASE("fake flash: the real page sizes and the program rules")
{
    FakeProgramFlash raw(0x08000000, 0x100000, 0x08080000);
    CHECK(raw.pageSize(0x0807F800) == 2048);
    CHECK(raw.pageSize(0x08080000) == 4096);
    REQUIRE(raw.eraseSector(0x0807F000));
    CHECK(raw.operations() == 2);
    REQUIRE(raw.eraseSector(0x08080000));
    CHECK(raw.operations() == 3);
    const std::uint8_t a[4] = {1, 2, 3, 4};
    const std::uint8_t zero[4] = {};
    REQUIRE(raw.program(0x08080000, a));
    CHECK_FALSE(raw.program(0x08080000, a));
    CHECK(raw.program(0x08080000, zero));
}

TEST_CASE("decision: every combination keeps something bootable or waits for the PC")
{
    const SlotState states[] = {SlotState::Empty, SlotState::Confirmed, SlotState::Trial, SlotState::Failed, SlotState::Committed};
    for (SlotState a : states) {
        for (SlotState b : states) {
            for (int attempts = 0; attempts <= 4; ++attempts) {
                for (int bits = 0; bits < 32; ++bits) {
                    BootState s;
                    s.a.state = a;
                    s.b.state = b;
                    s.attempts = static_cast<std::uint8_t>(attempts);
                    DecisionInputs in;
                    in.aGood = (bits & 1) != 0;
                    in.bGood = (bits & 2) != 0;
                    in.forceKey = (bits & 4) != 0;
                    in.request = (bits & 8) != 0 ? Request::Stay : (bits & 16) != 0 ? Request::Rollback
                                                                                    : Request::None;
                    const Decision d = decide(s, in);
                    CAPTURE(static_cast<int>(a));
                    CAPTURE(static_cast<int>(b));
                    CAPTURE(attempts);
                    CAPTURE(bits);
                    switch (d.action) {
                    case Action::StartConfirmed:
                        CHECK(a == SlotState::Confirmed);
                        CHECK(in.aGood);
                        break;
                    case Action::StartTrial:
                        CHECK(a == SlotState::Trial);
                        CHECK(in.aGood);
                        CHECK(attempts < 3);
                        break;
                    case Action::Install:
                        CHECK(b == SlotState::Committed);
                        CHECK(in.bGood);
                        break;
                    case Action::Revert:
                    case Action::Rollback:
                        CHECK(b == SlotState::Confirmed);
                        CHECK(in.bGood);
                        break;
                    case Action::DiscardDownload:
                        CHECK(b == SlotState::Committed);
                        CHECK_FALSE(in.bGood);
                        break;
                    case Action::MarkFailed:
                        CHECK(a == SlotState::Trial);
                        CHECK_FALSE((b == SlotState::Confirmed && in.bGood));
                        break;
                    case Action::LoaderMode:
                        break;
                    }
                    if (in.request == Request::Stay || in.forceKey) {
                        CHECK(d.action == Action::LoaderMode);
                    }
                    if (!spareMatters(s, in)) {
                        DecisionInputs other = in;
                        other.bGood = !in.bGood;
                        CHECK(decide(s, other) == d);
                    }
                }
            }
        }
    }
}

TEST_CASE("decision: the table")
{
    BootState s;
    DecisionInputs in;
    in.aGood = true;
    in.bGood = true;
    s.a.state = SlotState::Confirmed;
    CHECK(decide(s, in).action == Action::StartConfirmed);
    in.forceKey = true;
    CHECK(decide(s, in) == Decision{Action::LoaderMode, Fallback::None, StayReason::ForceKey});
    in.forceKey = false;
    in.request = Request::Stay;
    CHECK(decide(s, in).stay == StayReason::Requested);
    in.request = Request::Rollback;
    CHECK(decide(s, in).action == Action::StartConfirmed); // nothing to roll back to
    s.b.state = SlotState::Confirmed;
    CHECK(decide(s, in) == Decision{Action::Rollback, Fallback::Requested, StayReason::None});
    in.request = Request::None;
    s.b.state = SlotState::Committed;
    CHECK(decide(s, in).action == Action::Install);
    in.bGood = false;
    CHECK(decide(s, in).action == Action::DiscardDownload);
    s.b.state = SlotState::Confirmed;
    in.bGood = true;
    s.a.state = SlotState::Trial;
    s.attempts = 2;
    CHECK(decide(s, in).action == Action::StartTrial);
    s.attempts = 3;
    CHECK(decide(s, in) == Decision{Action::Revert, Fallback::AttemptsUsed, StayReason::None});
    s.attempts = 0;
    in.aGood = false;
    CHECK(decide(s, in) == Decision{Action::Revert, Fallback::CheckFailed, StayReason::None});
    s.b.state = SlotState::Empty;
    CHECK(decide(s, in) == Decision{Action::MarkFailed, Fallback::CheckFailed, StayReason::None});
    s.a.state = SlotState::Confirmed;
    CHECK(decide(s, in) == Decision{Action::LoaderMode, Fallback::None, StayReason::NothingBootable});
    s.a.state = SlotState::Failed;
    s.b.state = SlotState::Confirmed;
    CHECK(decide(s, in) == Decision{Action::Revert, Fallback::NoImage, StayReason::None});
}

TEST_CASE("loader: adopts the installed image into an empty record")
{
    Rig rig;
    installOld(rig);
    CHECK(rig.state().b.state == SlotState::Empty);
    CHECK(rig.untouched());
}

TEST_CASE("loader: an empty radio waits for the PC")
{
    Rig rig;
    const BootResult r = rig.settle();
    CHECK(r.outcome == Outcome::LoaderMode);
    CHECK(r.stay == StayReason::NothingBootable);
    CHECK(rig.recordValid());
}

TEST_CASE("loader: adopting survives a power cut at every point")
{
    everyCut([](Rig &rig) { rig.putInSlot(false, oldImage()); }, [](Rig &rig) { rig.settle(); },
        [](Rig &rig, long) { CHECK(rig.state().a.state == SlotState::Confirmed); }, true);
}

TEST_CASE("loader: install, trial, confirm")
{
    Rig rig;
    installOld(rig);
    REQUIRE(rig.commit(newImage(), ConfirmMode::Manual));
    CHECK(rig.settle().outcome == Outcome::StartImage);
    CHECK(rig.slotHolds(false, newImage()));
    CHECK(rig.slotHolds(true, oldImage()));
    BootState s = rig.state();
    CHECK(s.a.state == SlotState::Trial);
    CHECK(s.a.mode == ConfirmMode::Manual);
    CHECK(s.attempts == 1);
    CHECK(s.b.state == SlotState::Confirmed);
    REQUIRE(rig.confirm());
    for (int i = 0; i < 5; ++i) {
        CHECK(rig.settle().outcome == Outcome::StartImage);
    }
    s = rig.state();
    CHECK(s.a.state == SlotState::Confirmed);
    CHECK(rig.slotHolds(false, newImage()));
    CHECK(rig.untouched());
    // Each sector was erased about twice.
    for (std::uint32_t at = Small.slotA.begin; at < Small.slotA.end; at += rig.raw.pageSize(at)) {
        CHECK(rig.raw.erases(at) <= 2);
    }
}

TEST_CASE("loader: a trial image that never confirms is swapped back out")
{
    Rig rig;
    installOld(rig);
    REQUIRE(rig.commit(newImage(), ConfirmMode::Manual));
    for (int start = 1; start <= 3; ++start) {
        CHECK(rig.settle().outcome == Outcome::StartImage);
        CHECK(rig.state().a.state == SlotState::Trial);
        CHECK(rig.state().attempts == start);
    }
    CHECK(rig.settle().outcome == Outcome::StartImage);
    const BootState s = rig.state();
    CHECK(s.a.state == SlotState::Confirmed);
    CHECK(s.b.state == SlotState::Failed);
    CHECK(s.lastFallback == Fallback::AttemptsUsed);
    CHECK(rig.slotHolds(false, oldImage()));
    CHECK(rig.slotHolds(true, newImage()));
}

TEST_CASE("loader: rollback between two confirmed images, and back")
{
    Rig rig;
    installOld(rig);
    REQUIRE(rig.commit(newImage()));
    REQUIRE(rig.settle().outcome == Outcome::StartImage);
    REQUIRE(rig.confirm());
    CHECK(rig.settle({Request::Rollback}).outcome == Outcome::StartImage);
    CHECK(rig.slotHolds(false, oldImage()));
    CHECK(rig.state().b.state == SlotState::Confirmed);
    CHECK(rig.state().lastFallback == Fallback::Requested);
    CHECK(rig.settle({Request::Rollback}).outcome == Outcome::StartImage);
    CHECK(rig.slotHolds(false, newImage()));
    CHECK(rig.state().a.state == SlotState::Confirmed);
}

TEST_CASE("loader: a damaged trial image or download is not started")
{
    SUBCASE("trial image damaged after the swap")
    {
        Rig rig;
        installOld(rig);
        REQUIRE(rig.commit(newImage()));
        // Swap only: stop before the first start.
        Loader loader(rig.guarded, rig.layout, rig.buffer, rig.events);
        REQUIRE(loader.boot({}).outcome == Outcome::Reset);
        *rig.raw.at(Small.slotA.begin + 5000) ^= 0x40;
        CHECK(rig.settle().outcome == Outcome::StartImage);
        CHECK(rig.slotHolds(false, oldImage()));
        CHECK(rig.state().lastFallback == Fallback::CheckFailed);
    }
    SUBCASE("download damaged after the commit")
    {
        Rig rig;
        installOld(rig);
        REQUIRE(rig.commit(newImage()));
        *rig.raw.at(Small.slotB.begin + 300) &= 0x0F;
        CHECK(rig.settle().outcome == Outcome::StartImage);
        CHECK(rig.slotHolds(false, oldImage()));
        CHECK(rig.state().b.state == SlotState::Empty);
    }
    SUBCASE("nothing to fall back to")
    {
        Rig rig;
        REQUIRE(rig.settle().outcome == Outcome::LoaderMode);
        REQUIRE(rig.commit(newImage()));
        CHECK(rig.settle().outcome == Outcome::StartImage);
        CHECK(rig.slotHolds(false, newImage()));
        for (int i = 0; i < 3; ++i) {
            rig.settle();
        }
        const BootResult r = rig.settle();
        CHECK(r.outcome == Outcome::LoaderMode);
        CHECK(r.stay == StayReason::NothingBootable);
        CHECK(rig.state().a.state == SlotState::Failed);
        CHECK(rig.state().lastFallback == Fallback::AttemptsUsed);
    }
}

TEST_CASE("loader: the force key and a stay request wait for the PC")
{
    Rig rig;
    installOld(rig);
    CHECK(rig.settle({Request::None, true}).stay == StayReason::ForceKey);
    CHECK(rig.settle({Request::Stay}).stay == StayReason::Requested);
    CHECK(rig.settle().outcome == Outcome::StartImage);
}

TEST_CASE("loader: the test cut stops the swap, which then resumes")
{
    Rig rig;
    installOld(rig);
    REQUIRE(rig.commit(newImage()));
    BootInputs in;
    in.swapCut = 5;
    CHECK(rig.boot(in).outcome == Outcome::Reset);
    CHECK(rig.state().swap.active);
    CHECK(rig.state().swap.done == 5);
    CHECK(rig.settle().outcome == Outcome::StartImage);
    CHECK(rig.slotHolds(false, newImage()));
}

TEST_CASE("loader: install survives one power cut at every point, and two in a row")
{
    everyCut([](Rig &rig) { installOld(rig); },
        [](Rig &rig) {
            if (rig.commit(newImage(), ConfirmMode::Manual)) {
                rig.settle();
            }
        },
        [](Rig &rig, long) {
            const BootState s = rig.state();
            if (s.a.length == newImage().size()) {
                CHECK(s.a.state == SlotState::Trial);
                CHECK(rig.slotHolds(false, newImage()));
                CHECK(rig.slotHolds(true, oldImage()));
                CHECK(s.b.state == SlotState::Confirmed);
            } else {
                // Cut before the commit: the old image runs, nothing waits.
                CHECK(s.a.state == SlotState::Confirmed);
                CHECK(rig.slotHolds(false, oldImage()));
                CHECK(s.b.state == SlotState::Empty);
            }
        },
        true);
}

TEST_CASE("loader: the trial and the swap back survive a power cut at every point")
{
    everyCut(
        [](Rig &rig) {
            installOld(rig);
            REQUIRE(rig.commit(newImage(), ConfirmMode::Manual));
            REQUIRE(rig.settle().outcome == Outcome::StartImage);
        },
        [](Rig &rig) {
            for (int i = 0; i < 4 && rig.raw.powered(); ++i) {
                rig.settle();
            }
        },
        [](Rig &rig, long) {
            // More starts after the cut finish the fallback.
            for (int i = 0; i < 4; ++i) {
                rig.settle();
            }
            const BootState s = rig.state();
            CHECK(s.a.state == SlotState::Confirmed);
            CHECK(rig.slotHolds(false, oldImage()));
            CHECK(rig.slotHolds(true, newImage()));
            CHECK(s.b.state == SlotState::Failed);
        },
        true);
}

TEST_CASE("loader: confirming survives a power cut at every point")
{
    everyCut(
        [](Rig &rig) {
            installOld(rig);
            REQUIRE(rig.commit(newImage(), ConfirmMode::Manual));
            REQUIRE(rig.settle().outcome == Outcome::StartImage);
        },
        [](Rig &rig) { rig.confirm(); },
        [](Rig &rig, long) {
            const BootState s = rig.state();
            CHECK(rig.slotHolds(false, newImage()));
            CHECK((s.a.state == SlotState::Trial || s.a.state == SlotState::Confirmed));
        });
}

TEST_CASE("loader: a rollback request survives a power cut at every point")
{
    everyCut(
        [](Rig &rig) {
            installOld(rig);
            REQUIRE(rig.commit(newImage()));
            REQUIRE(rig.settle().outcome == Outcome::StartImage);
            REQUIRE(rig.confirm());
        },
        [](Rig &rig) { rig.settle({Request::Rollback}); },
        [](Rig &rig, long) {
            // Either the request was lost before the swap began, or the
            // swap finished; both images stay confirmed.
            const BootState s = rig.state();
            CHECK(s.a.state == SlotState::Confirmed);
            CHECK(s.b.state == SlotState::Confirmed);
            const bool rolledBack = rig.slotHolds(false, oldImage()) && rig.slotHolds(true, newImage());
            const bool unchanged = rig.slotHolds(false, newImage()) && rig.slotHolds(true, oldImage());
            CHECK((rolledBack || unchanged));
        },
        true);
}

TEST_CASE("loader: a swap that needs the record rewritten survives a power cut at every point")
{
    everyCut(
        [](Rig &rig) {
            installOld(rig);
            REQUIRE(rig.commit(newImage()));
            // Fill the record so the swap's entries run over its end.
            BootRecord record(rig.guarded, Small.record);
            REQUIRE(record.load());
            while (record.freeEntries() > 6) {
                REQUIRE(record.append(entry::fallback(Fallback::None)));
            }
        },
        [](Rig &rig) { rig.settle(); },
        [](Rig &rig, long) {
            CHECK(rig.slotHolds(false, newImage()));
            CHECK(rig.slotHolds(true, oldImage()));
            CHECK(rig.state().a.state == SlotState::Trial);
        },
        true);
}

TEST_CASE("loader: install and fall back on the real layout")
{
    namespace d = dmm::services::d878uv2;
    const BootLayout &layout = d::BootLayout;
    const auto oldBig = makeImage(layout, 290 * 1024 + 12, 11, "0.24.0");
    const auto newBig = makeImage(layout, 300 * 1024 + 4, 12, "0.25.0");
    auto setup = [&](Rig &rig) {
        rig.putInSlot(false, oldBig);
        REQUIRE(rig.settle().outcome == Outcome::StartImage);
        REQUIRE(rig.commit(newBig, ConfirmMode::Manual));
    };
    long total = 0;
    {
        Rig rig(layout, d::FlashBase, d::FlashEnd - d::FlashBase);
        setup(rig);
        rig.raw.powerOn();
        REQUIRE(rig.settle().outcome == Outcome::StartImage);
        total = rig.raw.operations();
        CHECK(rig.slotHolds(false, newBig));
        CHECK(rig.slotHolds(true, oldBig));
        CHECK(rig.untouched());
        for (int i = 0; i < 3; ++i) {
            rig.settle();
        }
        CHECK(rig.slotHolds(false, oldBig));
        CHECK(rig.state().b.state == SlotState::Failed);
        CHECK(rig.untouched());
    }
    for (long k = 1; k < total; k += 211) {
        CAPTURE(k);
        Rig rig(layout, d::FlashBase, d::FlashEnd - d::FlashBase);
        setup(rig);
        rig.raw.powerOn();
        rig.raw.cutAt(k);
        rig.settle();
        rig.raw.powerOn();
        REQUIRE(rig.settle().outcome == Outcome::StartImage);
        CHECK(rig.slotHolds(false, newBig));
        CHECK(rig.slotHolds(true, oldBig));
        CHECK(rig.untouched());
    }
}
