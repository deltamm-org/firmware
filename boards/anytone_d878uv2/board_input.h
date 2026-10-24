// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/input.h"

namespace board {

/// Keypad, side keys, PTT, channel knob and volume knob.
class BoardInput final : public dmm::services::InputDevices
{
public:
    /// Sets up the pins and the knob interrupt. Call once, before
    /// the scheduler starts.
    void configure();

    dmm::services::KeyMask readKeys() override;
    std::int32_t encoderPosition() override;
    int encoderCountsPerDetent() const override;
    int volumeKnob() override;
    /// As printed on the radio: P1, P2 on the front, PF1, PF2 on the side,
    /// PF3 on top.
    std::string_view keyLabel(dmm::services::Key key) const override;

    /// From the EXTI5_9 interrupt.
    static void onEncoderEdge();
};

} // namespace board
