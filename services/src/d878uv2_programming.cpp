// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_programming.h"

#include <string_view>

namespace dmm::services {

namespace {

constexpr std::string_view Enter = "PROGRAM";
constexpr std::string_view Leave = "END";
constexpr std::uint8_t Identify = 0x02;
constexpr std::uint8_t Ack = 0x06;
constexpr std::uint8_t Refused = 0x15;
// 'I', model, band code, version, 06. The band code says what the radio
// transmits on: 144-146 and 430-440 MHz. Codeplug tools accept only the
// versions they know.
constexpr std::uint8_t Identity[16] = {'I', 'D', '8', '7', '8', 'U', 'V', '2', 0x03, 'V', '1', '0', '1', 0, 0, Ack};
constexpr std::size_t ReadBytes = 6;
constexpr std::size_t WriteBytes = 24;

std::uint32_t bigEndian(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
        static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

} // namespace

bool D878Uv2Programming::active(os::Ticks now)
{
    if (m_program && now - m_lastAt >= IdleMs) {
        // The PC went away: what was not completed is dropped.
        m_changed = m_changed || m_writer.wrote();
        m_writer.abandon();
        leave();
    }
    return m_program;
}

void D878Uv2Programming::leave()
{
    m_program = false;
    m_have = 0;
}

void D878Uv2Programming::feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write)
{
    if (m_have != 0 && now - m_lastAt >= CommandTimeoutMs) {
        m_have = 0;
    }
    for (const std::uint8_t b : bytes) {
        if (!active(now)) {
            m_matched = b == static_cast<std::uint8_t>(Enter[m_matched]) ? m_matched + 1 : b == 'P' ? 1
                                                                                                    : 0;
            if (m_matched == Enter.size()) {
                m_matched = 0;
                m_program = true;
                m_have = 0;
                m_lastAt = now;
                constexpr std::uint8_t Answer[] = {'Q', 'X', Ack};
                write(Answer);
            }
            continue;
        }
        m_lastAt = now;
        m_command[m_have++] = b;
        command(write);
    }
}

// Acts on the command collected so far, once it is complete.
void D878Uv2Programming::command(Writer write)
{
    const std::uint8_t first = m_command[0];
    if (first == Identify) {
        m_have = 0;
        write(Identity);
    } else if (first == 'R') {
        if (m_have < ReadBytes) {
            return;
        }
        m_have = 0;
        const std::size_t length = m_command[5];
        if (length == 0 || length > MaxRead) {
            return;
        }
        std::uint8_t answer[6 + MaxRead + 2];
        answer[0] = 'W';
        for (std::size_t i = 1; i < 6; ++i) {
            answer[i] = m_command[i];
        }
        if (!readable(m_codeplug.read(bigEndian(&m_command[1]), std::span(&answer[6], length)))) {
            return;
        }
        std::uint8_t sum = 0;
        for (std::size_t i = 1; i < 6 + length; ++i) {
            sum = static_cast<std::uint8_t>(sum + answer[i]);
        }
        answer[6 + length] = sum;
        answer[7 + length] = Ack;
        write(std::span(answer, 8 + length));
    } else if (first == 'W') {
        if (m_have < WriteBytes) {
            return;
        }
        m_have = 0;
        // 'W', address, length 16, data, sum, 06.
        std::uint8_t sum = 0;
        for (std::size_t i = 1; i < 22; ++i) {
            sum = static_cast<std::uint8_t>(sum + m_command[i]);
        }
        const bool ok = m_command[5] == 16 && m_command[22] == sum && m_command[23] == Ack &&
            m_writer.write(bigEndian(&m_command[1]), std::span<const std::uint8_t>(&m_command[6], 16));
        write(std::span(ok ? &Ack : &Refused, 1));
    } else if (first == static_cast<std::uint8_t>(Leave[0])) {
        if (m_command[m_have - 1] != static_cast<std::uint8_t>(Leave[m_have - 1])) {
            m_have = 0;
        } else if (m_have == Leave.size()) {
            const bool wrote = m_writer.wrote();
            const bool ok = m_writer.finish();
            m_changed = m_changed || wrote;
            leave();
            write(std::span(ok ? &Ack : &Refused, 1));
        }
    } else {
        // Not a command: skipped.
        m_have = 0;
    }
}

} // namespace dmm::services
