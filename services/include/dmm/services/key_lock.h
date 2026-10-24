// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/input.h"

#include <cstdint>

namespace dmm::services {

/// Which controls the key lock covers, as the radio's settings choose.
struct KeyLockSettings
{
    bool knob = true;   // channel knob
    bool keypad = true; // front panel: digits, *, #, Exit, Up, Down, Front1, Front2
    bool side = true;   // Side1..Side3 and PTT
    /// Always locked, and no unlock on the radio.
    bool forced = false;
    /// Lock by itself after this long without a key on a lockable screen;
    /// 0 = never.
    std::uint32_t autoLockMs = 0;
};

/// The key lock: long * locks; Menu then * unlocks wherever keys are
/// refused, and never locks (a long * pressed within UnlockWindowMs of Menu
/// is ignored). Only screens that take the lock are locked. Releases always
/// pass, and nothing is locked while transmitting.
class KeyLock
{
public:
    /// How long * may follow Menu to unlock.
    static constexpr std::uint32_t UnlockWindowMs = 3000;

    enum class Verdict : std::uint8_t
    {
        Pass,         // handle the key normally
        Ignored,      // the rest of a press that locked or unlocked: drop it quietly
        Refused,      // locked: drop it and say so
        Locked,       // this key locked the keys; drop it
        UnlockPrompt, // Menu while locked: ask for *
        Unlocked,     // this key unlocked the keys; drop it
    };

    /// The situation a key arrives in.
    struct Context
    {
        bool screenLockable = false;
        bool transmitting = false;
        /// The key's programmable function always works (monitor, alarm
        /// and the like).
        bool exemptFunction = false;
        std::uint32_t nowMs = 0;
    };

    /// New settings; the auto lock's idle time starts again from the next
    /// update().
    void setSettings(const KeyLockSettings &settings)
    {
        m_settings = settings;
        m_restartIdle = true;
    }
    const KeyLockSettings &settings() const { return m_settings; }

    /// Locked by the user or by auto lock (forced lock aside).
    bool locked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

    /// A key event (any key but PTT).
    Verdict key(Key key, KeyDebouncer::EventType type, const Context &c);
    /// A channel knob click.
    Verdict knob(const Context &c);
    /// PTT pressed (true) or released. Release always passes.
    Verdict ptt(bool down, const Context &c);

    /// Moves time on; true when auto lock locked the keys just now.
    bool update(const Context &c);

private:
    bool active() const { return m_locked || m_settings.forced; }
    bool covers(Key key, bool exemptFunction) const;
    void noteActivity(std::uint32_t nowMs) { m_lastActivityMs = nowMs; }

    KeyLockSettings m_settings;
    bool m_locked = false;
    bool m_promptOpen = false;
    /// The rest of the press that locked or unlocked is dropped.
    bool m_swallow = false;
    Key m_swallowKey = Key::Count;
    std::uint32_t m_promptMs = 0;
    bool m_menuSeen = false;
    std::uint32_t m_menuMs = 0;
    /// The * now held was pressed within the window after Menu.
    bool m_starAfterMenu = false;
    bool m_restartIdle = true;
    std::uint32_t m_lastActivityMs = 0;
};

} // namespace dmm::services
