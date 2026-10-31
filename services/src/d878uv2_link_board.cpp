// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/d878uv2_link_board.h"

namespace dmm::services {

namespace {

constexpr std::string_view PacketHeader = "+APRS:";
constexpr std::uint8_t Flag = 0x7E;
// Two addresses, control, protocol and the check.
constexpr std::size_t MinFrame = 7 * 2 + 2 + 2;

} // namespace

D878Uv2LinkBoardReader::Result D878Uv2LinkBoardReader::feed(std::uint8_t byte)
{
    switch (m_state) {
    case State::Idle:
        if (byte == '+') {
            m_line[0] = '+';
            m_lineLength = 1;
            m_state = State::Header;
        }
        return Result::None;
    case State::Header:
        if (byte == '\n') {
            if (m_lineLength > 0 && m_line[m_lineLength - 1] == '\r') {
                --m_lineLength;
            }
            m_state = State::Idle;
            return Result::Line;
        }
        if (m_lineLength == m_line.size()) {
            m_state = State::Idle;
            return Result::None;
        }
        m_line[m_lineLength++] = static_cast<char>(byte);
        if (line() == PacketHeader) {
            m_state = State::FrameStart;
        }
        return Result::None;
    case State::FrameStart:
        m_frameLength = 0;
        m_state = byte == Flag ? State::Frame : State::Idle;
        return Result::None;
    case State::Frame:
        if (byte == Flag && m_frameLength >= MinFrame) {
            const std::size_t body = m_frameLength - 2;
            const auto check = static_cast<std::uint16_t>(m_frame[body] | m_frame[body + 1] << 8);
            if (ax25::frameCheck({m_frame.data(), body}) == check) {
                m_state = State::Idle;
                return Result::Frame;
            }
        }
        if (m_frameLength == m_frame.size()) {
            m_state = State::Idle;
            return Result::None;
        }
        m_frame[m_frameLength++] = byte;
        return Result::None;
    }
    return Result::None;
}

} // namespace dmm::services
