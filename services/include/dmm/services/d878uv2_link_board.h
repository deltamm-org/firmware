// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/ax25.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dmm::services {

/// What the D878UVII's BT+APRS board sends the radio over its serial link
/// (115200 baud, 8N1). Text messages are lines starting with '+' and
/// ending in CR LF, such as "+READY" once the board is up. A packet it
/// decoded comes as "+APRS:", a flag (0x7E), the AX.25 frame from the first
/// address to the frame check sequence, and a closing flag. The frame
/// itself may hold 0x7E bytes, so a flag ends it only where the frame check
/// before it is right.
class D878Uv2LinkBoardReader
{
public:
    /// Sent three times, 50 ms apart, after the board is powered: it then
    /// answers 0x06 and "+READY".
    static constexpr std::array<std::uint8_t, 3> StartCommand{0xEE, 0x01, 0x06};
    static constexpr std::size_t MaxLine = 64;

    enum class Result : std::uint8_t
    {
        None,  // the byte did not end anything
        Frame, // frame() holds a packet whose frame check is right
        Line,  // line() holds a text message
    };

    /// Takes one byte; says whether it ended a frame or a line.
    Result feed(std::uint8_t byte);
    /// Drops a partial message, for example after a pause in the data.
    void reset() { m_state = State::Idle; }

    /// The last frame, valid until the next feed().
    std::span<const std::uint8_t> frame() const { return {m_frame.data(), m_frameLength}; }
    /// The last line without CR LF, valid until the next feed().
    std::string_view line() const { return {m_line.data(), m_lineLength}; }

private:
    enum class State : std::uint8_t
    {
        Idle,
        Header,
        FrameStart,
        Frame,
    };

    State m_state = State::Idle;
    std::array<char, MaxLine> m_line{};
    std::size_t m_lineLength = 0;
    std::array<std::uint8_t, ax25::MaxFrameBytes> m_frame{};
    std::size_t m_frameLength = 0;
};

} // namespace dmm::services
