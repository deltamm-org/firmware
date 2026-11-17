// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <concepts>
#include <cstdint>
#include <span>

namespace dmm::services {

/// A modem that sends packet radio frames as audio into the transmitter
/// (1200 baud AFSK). The caller keys the transmitter around a send.
///
/// - send(frame, leadMs, levelPercent): starts sending @p frame (from the
///   first address to the frame check sequence) after @p leadMs of flags,
///   at @p levelPercent of the board's full audio level; false if a frame
///   is still going out.
/// - sending(): true until the last flag has been sent.
template <typename T>
concept PacketModem = requires(T &m, std::span<const std::uint8_t> frame, std::uint16_t leadMs, std::uint8_t levelPercent) {
    { m.send(frame, leadMs, levelPercent) } -> std::same_as<bool>;
    { m.sending() } -> std::same_as<bool>;
};

} // namespace dmm::services
