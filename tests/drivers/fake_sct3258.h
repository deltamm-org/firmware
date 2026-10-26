// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/host_port_bus.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// The chip side of the host port: after reset it takes 2048 raw bytes, then
// answers packets. Its status line reads "data waiting" from reset until
// the boot load, as the real chip's does, and while replies are queued.
class FakeSct3258 final : public dmm::hal::HostPortBus
{
public:
    bool answerBoot = true;
    std::uint8_t bootAck = 0x01;
    std::vector<std::vector<std::uint8_t>> packets; // fields of each packet received, type first
    std::size_t bootBytes = 0;
    bool oddWriteSeen = false;
    /// Packets (type, fields) the chip sends after the next request, before
    /// its reply.
    std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> reportsBeforeReply;

    void write(const std::uint8_t *bytes, std::size_t count) override
    {
        if (count % 2 != 0) {
            oddWriteSeen = true;
        }
        for (std::size_t i = 0; i < count; ++i) {
            take(bytes[i]);
        }
    }

    std::uint8_t read() override
    {
        if (m_out.empty()) {
            return 0x5A; // what the bus reads when nothing is queued
        }
        const std::uint8_t b = m_out.front();
        m_out.pop_front();
        return b;
    }

    bool dataWaiting() override { return bootBytes < 2048 || !m_out.empty(); }

    void queue(std::uint8_t type, std::vector<std::uint8_t> fields)
    {
        std::vector<std::uint8_t> p = {0x84, 0xA9, 0x61, static_cast<std::uint8_t>(fields.size() >> 8),
            static_cast<std::uint8_t>(fields.size()), type};
        p.insert(p.end(), fields.begin(), fields.end());
        if (p.size() % 2 != 0) {
            p.push_back(0);
        }
        m_out.insert(m_out.end(), p.begin(), p.end());
    }

private:
    void take(std::uint8_t b)
    {
        if (bootBytes < 2048) {
            if (++bootBytes == 2048 && answerBoot) {
                queue(0x20, {0x17, bootAck});
            }
            return;
        }
        m_in.push_back(b);
        if (m_in.size() >= 6 && m_in[0] == 0x84 && m_in[1] == 0xA9 && m_in[2] == 0x61) {
            const std::size_t length = static_cast<std::size_t>(m_in[3] << 8 | m_in[4]);
            std::size_t total = 6 + length;
            total += total % 2;
            if (m_in.size() == total) {
                std::vector<std::uint8_t> fields(m_in.begin() + 5, m_in.begin() + 6 + static_cast<std::ptrdiff_t>(length));
                packets.push_back(fields);
                answer(fields);
                m_in.clear();
            }
        } else if (m_in.size() >= 3 && !(m_in[0] == 0x84 && m_in[1] == 0xA9 && m_in[2] == 0x61)) {
            m_in.erase(m_in.begin());
        }
    }

    void answer(const std::vector<std::uint8_t> &p)
    {
        for (const auto &[t, f] : reportsBeforeReply) {
            queue(t, f);
        }
        reportsBeforeReply.clear();
        const std::uint8_t type = p[0];
        const std::uint8_t field = p.size() > 1 ? p[1] : 0;
        if (type == 0x40) {
            queue(0x40, {0x17, 0x00});
        } else if (field >= 0x30 && field <= 0x32 && type != 0x20) {
            queue(0x00, {0x17, 0x01}); // a query without the read bit, as the real chip answers it
        } else if (field >= 0x30 && field <= 0x32) {
            const std::string text = field == 0x30 ? "CT3258F" : field == 0x31 ? "V0.02.02"
                                                                               : "V2.01.07";
            std::vector<std::uint8_t> reply = {field, static_cast<std::uint8_t>(text.size())};
            reply.insert(reply.end(), text.begin(), text.end());
            queue(0x20, reply);
        } else if (field == 0x7E) {
            queue(0x00, {0x17, 0x04}); // unknown command
        } else {
            queue(0x00, {field, 0x00});
        }
    }

    std::vector<std::uint8_t> m_in;
    std::deque<std::uint8_t> m_out;
};
