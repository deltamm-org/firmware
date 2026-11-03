// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/boot_loader.h"

namespace dmm::services::boot {

namespace {

bool spareGood(const BootState &s, const DecisionInputs &in)
{
    return s.b.state == SlotState::Confirmed && in.bGood;
}

Decision stay(StayReason why)
{
    return {Action::LoaderMode, Fallback::None, why};
}

Decision act(Action action, Fallback reason = Fallback::None)
{
    return {action, reason, StayReason::None};
}

} // namespace

Decision decide(const BootState &s, const DecisionInputs &in)
{
    if (in.request == Request::Stay) {
        return stay(StayReason::Requested);
    }
    if (in.forceKey) {
        return stay(StayReason::ForceKey);
    }
    if (in.request == Request::Rollback && spareGood(s, in)) {
        return act(Action::Rollback, Fallback::Requested);
    }
    if (s.b.state == SlotState::Committed) {
        return act(in.bGood ? Action::Install : Action::DiscardDownload);
    }
    switch (s.a.state) {
    case SlotState::Trial: {
        Fallback why = Fallback::None;
        if (!in.aGood) {
            why = Fallback::CheckFailed;
        } else if (s.attempts >= in.maxAttempts) {
            why = Fallback::AttemptsUsed;
        } else {
            return act(Action::StartTrial);
        }
        return act(spareGood(s, in) ? Action::Revert : Action::MarkFailed, why);
    }
    case SlotState::Confirmed:
        if (in.aGood) {
            return act(Action::StartConfirmed);
        }
        return spareGood(s, in) ? act(Action::Revert, Fallback::CheckFailed) : stay(StayReason::NothingBootable);
    case SlotState::Empty:
    case SlotState::Failed:
    case SlotState::Committed:
        break;
    }
    return spareGood(s, in) ? act(Action::Revert, Fallback::NoImage) : stay(StayReason::NothingBootable);
}

bool spareMatters(const BootState &s, const DecisionInputs &in)
{
    if (s.b.state == SlotState::Committed) {
        return true;
    }
    if (s.b.state != SlotState::Confirmed) {
        return false;
    }
    if (in.request == Request::Rollback) {
        return true;
    }
    switch (s.a.state) {
    case SlotState::Trial:
        return !in.aGood || s.attempts >= in.maxAttempts;
    case SlotState::Confirmed:
        return !in.aGood;
    default:
        return true;
    }
}

Loader::Loader(ProgramFlash &flash, const BootLayout &layout, std::span<std::uint8_t, SectorBytes> buffer, LoaderEvents &events)
    : m_flash(flash), m_layout(layout), m_buffer(buffer), m_events(events), m_record(flash, layout.record)
{
}

bool Loader::slotGood(bool slotB, bool fullCheck)
{
    const SlotRecord &r = slotB ? m_record.state().b : m_record.state().a;
    if (r.state == SlotState::Empty || r.state == SlotState::Failed) {
        return false;
    }
    const std::uint32_t at = slotB ? m_layout.slotB.begin : m_layout.slotA.begin;
    return checkImage(m_flash, m_layout, at, r.length, fullCheck, r.crc) == ImageProblem::None;
}

bool Loader::adopt()
{
    BootState s;
    const std::uint32_t base = m_layout.slotA.begin;
    std::uint8_t infoBytes[ImageInfoBytes];
    ImageInfo info;
    if (m_flash.read(base + m_layout.infoOffset, infoBytes) && parseImageInfo(infoBytes, info) && info.end > info.base) {
        const std::uint32_t length = info.end - info.base;
        std::uint32_t crc = 0;
        if (checkImage(m_flash, m_layout, base, length, false, 0) == ImageProblem::None &&
            flashCrc(m_flash, base, length, crc)) {
            s.a = {SlotState::Confirmed, ConfirmMode::Automatic, length, crc};
        }
    }
    m_events.busy(true);
    const bool ok = m_record.rewrite(s);
    m_events.busy(false);
    return ok;
}

bool Loader::beginSwap(SwapKind kind, Fallback reason)
{
    const BootState &s = m_record.state();
    const std::uint32_t a = s.a.state == SlotState::Empty ? 0 : sectorsFor(s.a.length);
    const std::uint32_t b = sectorsFor(s.b.length);
    const std::uint32_t sectors = a > b ? a : b;
    if (sectors == 0 || sectors > (m_layout.slotB.end - m_layout.slotB.begin) / SectorBytes ||
        sectors + 1 > (m_layout.slotA.end - m_layout.slotA.begin) / SectorBytes) {
        return false;
    }
    return m_record.reserve(2) && m_record.append(entry::swapBegin(kind, reason, static_cast<std::uint16_t>(sectors)));
}

bool Loader::copySector(std::uint32_t from, std::uint32_t to)
{
    if (!m_flash.read(from, m_buffer) || !m_flash.eraseSector(to)) {
        return false;
    }
    m_events.alive();
    if (!m_flash.program(to, m_buffer)) {
        return false;
    }
    std::uint8_t back[256];
    for (std::uint32_t done = 0; done < SectorBytes; done += sizeof back) {
        if (!m_flash.read(to + done, back)) {
            return false;
        }
        for (std::size_t i = 0; i < sizeof back; ++i) {
            if (back[i] != m_buffer[done + i]) {
                return false;
            }
        }
    }
    m_events.alive();
    return true;
}

// Slot A has one sector more than the largest image. First every sector
// of A moves up by one, from the top. Then for each sector i, B[i] goes to
// A[i] and the moved old A[i] (now at A[i + 1]) to B[i]. Each step's source
// stays intact until the step after it, so a step cut short is redone.
bool Loader::runSwap(std::uint16_t cutAfter, bool &cut)
{
    cut = false;
    const std::uint32_t n = m_record.state().swap.sectors;
    const std::uint32_t a = m_layout.slotA.begin;
    const std::uint32_t b = m_layout.slotB.begin;
    std::uint16_t stepsRun = 0;
    for (std::uint32_t step = m_record.state().swap.done; step < 3 * n; ++step) {
        if (cutAfter != 0 && stepsRun == cutAfter) {
            cut = true;
            return true;
        }
        std::uint32_t from = 0;
        std::uint32_t to = 0;
        if (step < n) {
            const std::uint32_t i = n - 1 - step;
            from = a + i * SectorBytes;
            to = a + (i + 1) * SectorBytes;
        } else {
            const std::uint32_t j = step - n;
            const std::uint32_t i = j / 2;
            if (j % 2 == 0) {
                from = b + i * SectorBytes;
                to = a + i * SectorBytes;
            } else {
                from = a + (i + 1) * SectorBytes;
                to = b + i * SectorBytes;
            }
        }
        if (!copySector(from, to) || !m_record.reserve(1) ||
            !m_record.append(entry::swapStep(static_cast<std::uint16_t>(step)))) {
            return false;
        }
        ++stepsRun;
    }
    return m_record.reserve(1) && m_record.append(entry::swapEnd());
}

bool Loader::eraseDownload(std::uint32_t bytes)
{
    const std::uint32_t capacity = m_layout.slotB.end - m_layout.slotB.begin;
    const std::uint32_t sectors = sectorsFor(bytes == 0 || bytes > capacity ? capacity : bytes);
    SlotRecord empty;
    if (!m_record.reserve(1) || !m_record.append(entry::slot(true, empty))) {
        return false;
    }
    m_events.busy(true);
    bool ok = true;
    for (std::uint32_t i = 0; i < sectors && ok; ++i) {
        ok = m_flash.eraseSector(m_layout.slotB.begin + i * SectorBytes);
        m_events.alive();
    }
    m_events.busy(false);
    return ok;
}

BootResult Loader::boot(const BootInputs &in)
{
    if (!m_record.load() && !adopt()) {
        return {Outcome::LoaderMode, StayReason::FlashError};
    }
    // Each pass either ends the start-up or changes the record, which moves
    // towards an end; the limit only guards against a bug.
    for (int pass = 0; pass < 8; ++pass) {
        const BootState &s = m_record.state();
        if (s.swap.active) {
            bool cut = false;
            m_events.busy(true);
            const bool ok = runSwap(in.swapCut, cut);
            m_events.busy(false);
            if (!ok) {
                return {Outcome::LoaderMode, StayReason::FlashError};
            }
            return {Outcome::Reset, StayReason::None};
        }

        DecisionInputs d;
        d.request = in.request;
        d.forceKey = in.forceKey;
        d.maxAttempts = MaxAttempts;
        // A new image gets its CRC checked before its first start.
        d.aGood = slotGood(false, s.a.state == SlotState::Trial && s.attempts == 0);
        d.bGood = spareMatters(s, d) && slotGood(true, true);
        const Decision decision = decide(s, d);

        bool ok = true;
        switch (decision.action) {
        case Action::LoaderMode:
            return {Outcome::LoaderMode, decision.stay};
        case Action::StartConfirmed:
            if (!m_record.reserve(ReserveForImage)) {
                return {Outcome::LoaderMode, StayReason::FlashError};
            }
            return {Outcome::StartImage, StayReason::None};
        case Action::StartTrial:
            if (!m_record.reserve(ReserveForImage + 1) ||
                !m_record.append(entry::attempt(static_cast<std::uint8_t>(s.attempts + 1)))) {
                return {Outcome::LoaderMode, StayReason::FlashError};
            }
            return {Outcome::StartImage, StayReason::None};
        case Action::Install:
            ok = beginSwap(SwapKind::Install, Fallback::None);
            break;
        case Action::Revert:
            ok = beginSwap(SwapKind::Revert, decision.reason);
            break;
        case Action::Rollback:
            ok = beginSwap(SwapKind::Rollback, Fallback::Requested);
            break;
        case Action::DiscardDownload:
            ok = m_record.reserve(1) && m_record.append(entry::slot(true, {}));
            break;
        case Action::MarkFailed: {
            SlotRecord failed = s.a;
            failed.state = SlotState::Failed;
            ok = m_record.reserve(2) && m_record.append(entry::slot(false, failed)) &&
                m_record.append(entry::fallback(decision.reason));
            break;
        }
        }
        if (!ok) {
            return {Outcome::LoaderMode, StayReason::FlashError};
        }
    }
    return {Outcome::LoaderMode, StayReason::FlashError};
}

} // namespace dmm::services::boot
