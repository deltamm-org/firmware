// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

namespace dmm::services {

/// Debounces the receiver's carrier decision into speaker open or closed:
/// it opens after OpenAfter carrier polls in a row and closes after
/// CloseAfter polls without, so a single odd poll changes nothing. Forced
/// open (squelch off) it ignores the carrier.
class SquelchGate
{
public:
    static constexpr int OpenAfter = 3;
    static constexpr int CloseAfter = 3;

    /// One poll. Returns whether the speaker should be open.
    bool update(bool carrier)
    {
        if (carrier == m_open) {
            m_count = 0;
        } else if (++m_count >= (carrier ? OpenAfter : CloseAfter)) {
            m_open = carrier;
            m_count = 0;
        }
        return open();
    }

    void setForcedOpen(bool forced) { m_forced = forced; }
    bool open() const { return m_forced || m_open; }
    /// Closed, counts cleared; for a new channel.
    void reset()
    {
        m_open = false;
        m_count = 0;
    }

private:
    bool m_open = false;
    bool m_forced = false;
    int m_count = 0;
};

} // namespace dmm::services
