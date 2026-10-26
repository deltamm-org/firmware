// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

namespace dmm::services {

/// Debounces one receiver decision, a carrier or a tone, into open or
/// closed: it opens after openAfter polls with it in a row and closes after
/// closeAfter polls without, so a single odd poll changes nothing. Forced
/// open (squelch off) it ignores the input.
class SquelchGate
{
public:
    explicit SquelchGate(int openAfter = 3, int closeAfter = 3)
        : m_openAfter(openAfter), m_closeAfter(closeAfter)
    {
    }

    /// One poll. Returns whether it is open.
    bool update(bool heard)
    {
        if (heard == m_open) {
            m_count = 0;
        } else if (++m_count >= (heard ? m_openAfter : m_closeAfter)) {
            m_open = heard;
            m_count = 0;
        }
        return open();
    }

    /// New poll counts, from the next poll on.
    void setCounts(int openAfter, int closeAfter)
    {
        m_openAfter = openAfter;
        m_closeAfter = closeAfter;
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
    int m_openAfter;
    int m_closeAfter;
    bool m_open = false;
    bool m_forced = false;
    int m_count = 0;
};

} // namespace dmm::services
