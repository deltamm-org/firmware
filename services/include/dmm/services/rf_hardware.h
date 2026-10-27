// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>

namespace dmm::services {

/// The board's RF controls outside the transceiver chip. Everything starts
/// off: no power, both enables low.
class RfHardware
{
public:
    /// Reference oscillator trim, from the radio's calibration.
    virtual void setReferenceTrim(std::uint16_t value) = 0;
    /// Receive front-end tuning, from the radio's calibration.
    virtual void setReceiveTune(std::uint16_t value) = 0;
    /// Transmit path switched in (true) or out.
    virtual void setTransmitPath(bool on) = 0;
    /// Power amplifier enabled.
    virtual void setPowerAmplifier(bool on) = 0;
    /// Power amplifier output control, from the radio's calibration; 0 = no
    /// output.
    virtual void setPowerControl(std::uint16_t value) = 0;
    /// Digital transmit: the transmit path and the power amplifier, at
    /// output control @p value, follow the baseband's transmit slots from
    /// now on. 0 stops that and switches both off at once.
    virtual void setSlotKeying(std::uint16_t value) = 0;

protected:
    ~RfHardware() = default;
};

} // namespace dmm::services
