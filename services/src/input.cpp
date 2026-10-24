// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/input.h"

namespace dmm::services {

std::string_view keyName(Key key)
{
    static constexpr std::string_view Names[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#", "Menu",
        "Exit", "Up", "Down", "Front1", "Front2", "Side1", "Side2", "Side3", "PTT"};
    static_assert(sizeof Names / sizeof Names[0] == static_cast<unsigned>(Key::Count));
    const auto i = static_cast<unsigned>(key);
    return i < static_cast<unsigned>(Key::Count) ? Names[i] : "?";
}

namespace {

bool repeats(Key key)
{
    return key == Key::Up || key == Key::Down;
}

bool reached(os::Ticks now, os::Ticks at)
{
    return static_cast<std::int32_t>(now - at) >= 0;
}

} // namespace

void KeyDebouncer::update(KeyMask raw, os::Ticks now, lib::FnRef<void(const Event &)> onEvent)
{
    // The whole mask must hold still before it is taken, so keys pressed
    // together are also reported together.
    if (raw != m_candidate) {
        m_candidate = raw;
        m_candidateCount = 1;
    } else if (m_candidateCount < StableSamples) {
        ++m_candidateCount;
    }
    if (m_candidateCount >= StableSamples && m_candidate != m_stable) {
        const KeyMask changed = m_candidate ^ m_stable;
        m_stable = m_candidate;
        for (unsigned i = 0; i < static_cast<unsigned>(Key::Count); ++i) {
            if ((changed & (1u << i)) == 0) {
                continue;
            }
            const auto key = static_cast<Key>(i);
            if ((m_stable & (1u << i)) != 0) {
                m_timing[i] = {now, now + RepeatDelayMs, false};
                onEvent({key, EventType::Press});
            } else {
                onEvent({key, EventType::Release});
            }
        }
    }

    for (unsigned i = 0; i < static_cast<unsigned>(Key::Count); ++i) {
        if ((m_stable & (1u << i)) == 0) {
            continue;
        }
        const auto key = static_cast<Key>(i);
        KeyTiming &t = m_timing[i];
        if (!t.longSent && reached(now, t.pressedAt + LongPressMs)) {
            t.longSent = true;
            onEvent({key, EventType::LongPress});
        }
        if (repeats(key) && reached(now, t.nextRepeat)) {
            t.nextRepeat += RepeatMs;
            onEvent({key, EventType::Repeat});
        }
    }
}

} // namespace dmm::services
