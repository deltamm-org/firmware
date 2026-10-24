// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/input.h"

#include <atomic>
#include <iterator>
#include <string_view>

/// Simulated keys and knobs, set from the GUI thread and read by the app.
class SimInput final : public dmm::services::InputDevices
{
public:
    void setKey(dmm::services::Key key, bool down);
    /// Turns the channel knob by @p clicks (positive = clockwise).
    void turn(int clicks);
    /// 0..1000.
    void setVolume(int value);

    dmm::services::KeyMask readKeys() override { return m_keys.load(); }
    std::int32_t encoderPosition() override { return m_position.load(); }
    int encoderCountsPerDetent() const override { return CountsPerDetent; }
    int volumeKnob() override { return m_volume.load(); }
    /// The simulated panel's labels.
    std::string_view keyLabel(dmm::services::Key key) const override
    {
        static constexpr std::string_view Labels[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#", "Menu",
            "Exit", "Up", "Down", "P1", "P2", "PF1", "PF2", "PF3", "PTT"};
        static_assert(std::size(Labels) == static_cast<unsigned>(dmm::services::Key::Count));
        const auto i = static_cast<unsigned>(key);
        return i < std::size(Labels) ? Labels[i] : "?";
    }

private:
    static constexpr int CountsPerDetent = 2;

    std::atomic<dmm::services::KeyMask> m_keys{0};
    std::atomic<std::int32_t> m_position{0};
    std::atomic<int> m_volume{500};
};
