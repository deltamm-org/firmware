// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/services/ax25.h"
#include "dmm/services/log.h"

#include <cstdint>
#include <span>

/// Logs each frame instead of sending it; done at once.
class SimModem
{
public:
    bool send(std::span<const std::uint8_t> frame, std::uint16_t leadMs, std::uint8_t levelPercent)
    {
        dmm::services::ax25::Frame f;
        auto line = dmm::services::log::info("modem");
        if (dmm::services::ax25::decode(frame, f)) {
            dmm::services::ax25::writeText(line, f);
        } else {
            line << "a frame that does not decode";
        }
        line << " (" << leadMs << " ms lead, level " << levelPercent << " %)";
        return true;
    }
    bool sending() const { return false; }
};
