// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/aprs.h"
#include "dmm/services/ax25.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string_view>

/// A BT+APRS board that hears made-up stations near the simulated GPS
/// position: it answers the start with "+READY", then sends one of them
/// every 20 s as the board does.
class SimAprsLink
{
public:
    void start()
    {
        std::lock_guard guard(m_lock);
        m_on = true;
        m_last = std::chrono::steady_clock::now();
        put("\x06+READY\r\n");
    }
    void stop()
    {
        std::lock_guard guard(m_lock);
        m_on = false;
        m_out.clear();
    }
    std::size_t read(std::span<std::uint8_t> data)
    {
        std::lock_guard guard(m_lock);
        const auto now = std::chrono::steady_clock::now();
        if (m_on && now - m_last >= std::chrono::seconds(20)) {
            m_last = now;
            hear(m_next++ % 3);
        }
        std::size_t n = 0;
        while (n < data.size() && !m_out.empty()) {
            data[n++] = m_out.front();
            m_out.pop_front();
        }
        return n;
    }
    void write(std::span<const std::uint8_t>) {}
    std::uint32_t lostBytes() const { return 0; }

private:
    void put(std::string_view text) { m_out.insert(m_out.end(), text.begin(), text.end()); }

    void hear(unsigned which)
    {
        namespace ax25 = dmm::services::ax25;
        ax25::Frame f;
        constexpr const char *Calls[] = {"N0CALL-7", "N0CALL-9", "N0CALL-1"};
        f.source = *ax25::parseAddress(Calls[which]);
        f.destination = *ax25::parseAddress("APZSIM");
        f.path.pushBack(*ax25::parseAddress("WIDE1-1*"));
        if (which == 2) {
            f.info << ">Made-up status text";
        } else {
            dmm::services::aprs::Position p;
            p.latitudeE7 = 514780000 + static_cast<std::int32_t>(which) * 90000;
            p.longitudeE7 = -15000 + static_cast<std::int32_t>(which) * 160000;
            p.symbol = which == 0 ? '>' : '-';
            p.hasCourse = which == 0;
            p.courseDeg = 90;
            p.speedKnots = 27;
            p.hasAltitude = true;
            p.altitudeM = 40;
            p.comment << (which == 0 ? "Made-up car" : "Made-up home");
            dmm::services::aprs::positionReport(p, false, f.info);
        }
        std::array<std::uint8_t, ax25::MaxFrameBytes> bytes{};
        const std::size_t n = ax25::encode(f, bytes);
        put("+APRS:~");
        m_out.insert(m_out.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
        m_out.push_back(0x7E);
    }

    std::mutex m_lock;
    std::deque<std::uint8_t> m_out;
    bool m_on = false;
    std::chrono::steady_clock::time_point m_last;
    unsigned m_next = 0;
};
