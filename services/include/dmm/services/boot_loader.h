// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The loader's logic: what to do at each start, the swap of the two slots
// and the trial of a new image. Works on any ProgramFlash, so the same
// code runs on the radio, in the host tests and in the loader simulator.

#include "dmm/services/boot_flash.h"
#include "dmm/services/boot_image.h"
#include "dmm/services/boot_record.h"

#include <cstdint>
#include <span>

namespace dmm::services::boot {

/// What the image asked for before restarting, already dropped when the
/// reset was not a software or watchdog reset.
enum class Request : std::uint8_t
{
    None,
    Stay,
    Rollback,
};

/// Why the loader waits for the PC.
enum class StayReason : std::uint8_t
{
    None,
    Requested,
    ForceKey,
    NothingBootable,
    FlashError,
};

enum class Action : std::uint8_t
{
    LoaderMode,
    StartConfirmed,
    StartTrial,
    Install,
    Revert,
    Rollback,
    /// Slot B's download fails its check: forget it.
    DiscardDownload,
    /// The trial image failed and nothing can replace it.
    MarkFailed,
};

struct DecisionInputs
{
    Request request = Request::None;
    /// The force key was held at power-on.
    bool forceKey = false;
    /// Slot A's image passes its check.
    bool aGood = false;
    /// Slot B's image passes its check.
    bool bGood = false;
    std::uint8_t maxAttempts = 3;
};

struct Decision
{
    Action action = Action::LoaderMode;
    Fallback reason = Fallback::None;
    StayReason stay = StayReason::None;

    bool operator==(const Decision &) const = default;
};

/// The boot decision, for a record without a swap in progress.
Decision decide(const BootState &state, const DecisionInputs &in);

/// True when decide() can depend on bGood for these inputs, so slot B
/// needs checking.
bool spareMatters(const BootState &state, const DecisionInputs &in);

/// Board hooks the loader calls during long flash work.
class LoaderEvents
{
public:
    /// Flash work starts (true) or ends (false): keep the power on, show it.
    virtual void busy(bool on) = 0;
    /// Called often during flash work: feed the watchdog.
    virtual void alive() = 0;

protected:
    ~LoaderEvents() = default;
};

enum class Outcome : std::uint8_t
{
    /// Start the image in slot A.
    StartImage,
    /// Restart the MCU; the next start goes on from the record.
    Reset,
    /// Wait for the PC.
    LoaderMode,
};

struct BootResult
{
    Outcome outcome = Outcome::LoaderMode;
    StayReason stay = StayReason::None;
};

struct BootInputs
{
    Request request = Request::None;
    bool forceKey = false;
    /// Test: stop the swap with Reset after this many steps; 0 = never.
    std::uint16_t swapCut = 0;
};

class Loader
{
public:
    static constexpr std::uint8_t MaxAttempts = 3;
    /// Entries left free for the image to append its confirmation.
    static constexpr std::uint32_t ReserveForImage = 4;

    /// @p flash must already be guarded. @p buffer holds one sector.
    Loader(ProgramFlash &flash, const BootLayout &layout, std::span<std::uint8_t, SectorBytes> buffer, LoaderEvents &events);

    /// Runs the start-up: reads the record (adopting slot A's image into an
    /// empty one), finishes a swap in progress, decides and acts.
    BootResult boot(const BootInputs &in);

    BootRecord &record() { return m_record; }
    const BootLayout &layout() const { return m_layout; }
    ProgramFlash &flash() { return m_flash; }
    LoaderEvents &events() { return m_events; }

    /// Checks the image in a slot against its record. With @p fullCheck the
    /// CRC is checked too.
    bool slotGood(bool slotB, bool fullCheck);

    /// Erases the first @p bytes of slot B (whole sectors; 0 = all of it).
    bool eraseDownload(std::uint32_t bytes);

private:
    bool adopt();
    bool beginSwap(SwapKind kind, Fallback reason);
    /// Runs the swap from where the record says; false on a flash error.
    /// Sets @p cut when it stopped early for the test.
    bool runSwap(std::uint16_t cutAfter, bool &cut);
    bool copySector(std::uint32_t from, std::uint32_t to);

    ProgramFlash &m_flash;
    const BootLayout &m_layout;
    std::span<std::uint8_t, SectorBytes> m_buffer;
    LoaderEvents &m_events;
    BootRecord m_record;
};

} // namespace dmm::services::boot
