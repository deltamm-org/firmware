// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/rf_hardware.h"

namespace board {

/// The RF board's MCU-side controls: power-amplifier control voltage
/// (DAC0), reference trim (DAC1), front-end tuning (PWM) and the two
/// transmit enables.
class BoardRf final : public dmm::services::RfHardware
{
public:
    /// Power control at 0 and both enables low. The reference trim pin
    /// stays an input until a trim value is set.
    void configure();

    void setReferenceTrim(std::uint16_t value) override;
    void setReceiveTune(std::uint16_t value) override;
    void setTransmitPath(bool on) override;
    void setPowerAmplifier(bool on) override;
    void setPowerControl(std::uint16_t value) override;
    void setSlotKeying(std::uint16_t value, bool shaped) override;

    /// From the transmit slot line's interrupt.
    static void onSlotEdge();
    /// From the output ramp's step timer interrupt (TIMER6).
    static void onRampStep();

private:
    bool m_trimStarted = false;
};

} // namespace board
