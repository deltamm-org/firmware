// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/key_lock.h"

namespace dmm::services {

namespace {

using Type = KeyDebouncer::EventType;

bool elapsed(std::uint32_t now, std::uint32_t since, std::uint32_t ms)
{
    return now - since >= ms;
}

} // namespace

bool KeyLock::covers(Key key, bool exemptFunction) const
{
    switch (key) {
    case Key::Side1:
    case Key::Side2:
    case Key::Side3:
        return m_settings.side && !exemptFunction;
    case Key::Front1:
    case Key::Front2:
        return m_settings.keypad && !exemptFunction;
    case Key::Menu:
    case Key::Ptt:
        return false;
    default:
        return m_settings.keypad && key < Key::Count;
    }
}

KeyLock::Verdict KeyLock::key(Key key, Type type, const Context &c)
{
    noteActivity(c.nowMs);
    const bool fresh = type == Type::Press;
    if (m_swallow && key == m_swallowKey && !fresh) {
        if (type == Type::Release) {
            m_swallow = false;
        }
        return type == Type::Release ? Verdict::Pass : Verdict::Ignored;
    }
    if (type == Type::Release) {
        return Verdict::Pass;
    }
    if (key == Key::Menu && fresh) {
        m_menuSeen = true;
        m_menuMs = c.nowMs;
    }
    if (key == Key::Star && fresh) {
        m_starAfterMenu = m_menuSeen && !elapsed(c.nowMs, m_menuMs, UnlockWindowMs);
    }
    const bool gated = active() && c.screenLockable && !c.transmitting;
    if (gated && key == Key::Menu) {
        // Menu offers the unlock where keys are locked; under forced lock
        // there is none.
        if (!fresh || m_settings.forced) {
            return Verdict::Refused;
        }
        m_promptOpen = true;
        m_promptMs = c.nowMs;
        return Verdict::UnlockPrompt;
    }
    if (m_locked && key == Key::Star && fresh && m_promptOpen && !elapsed(c.nowMs, m_promptMs, UnlockWindowMs)) {
        m_promptOpen = false;
        m_locked = false;
        m_swallow = true;
        m_swallowKey = key;
        return Verdict::Unlocked;
    }
    if (fresh) {
        m_promptOpen = false;
    }
    if (gated && covers(key, c.exemptFunction)) {
        return Verdict::Refused;
    }
    if (!active() && key == Key::Star && type == Type::LongPress && c.screenLockable && !c.transmitting &&
        !m_starAfterMenu) {
        m_locked = true;
        m_swallow = true;
        m_swallowKey = key;
        return Verdict::Locked;
    }
    return Verdict::Pass;
}

KeyLock::Verdict KeyLock::knob(const Context &c)
{
    noteActivity(c.nowMs);
    if (active() && m_settings.knob && c.screenLockable && !c.transmitting) {
        return Verdict::Refused;
    }
    return Verdict::Pass;
}

KeyLock::Verdict KeyLock::ptt(bool down, const Context &c)
{
    noteActivity(c.nowMs);
    if (!down) {
        return Verdict::Pass;
    }
    if (active() && m_settings.side && c.screenLockable) {
        return Verdict::Refused;
    }
    return Verdict::Pass;
}

bool KeyLock::update(const Context &c)
{
    if (m_restartIdle) {
        m_restartIdle = false;
        noteActivity(c.nowMs);
    }
    if (m_promptOpen && elapsed(c.nowMs, m_promptMs, UnlockWindowMs)) {
        m_promptOpen = false;
    }
    if (m_locked || m_settings.autoLockMs == 0 || !c.screenLockable || c.transmitting) {
        if (c.transmitting) {
            noteActivity(c.nowMs);
        }
        return false;
    }
    if (!elapsed(c.nowMs, m_lastActivityMs, m_settings.autoLockMs)) {
        return false;
    }
    m_locked = true;
    return true;
}

} // namespace dmm::services
