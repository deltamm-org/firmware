// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// The board's sampler of the received audio; the board names the type
/// (dmm/board/parts.h).
///
/// - start(rateHz): starts sampling in the background; false if the board
///   cannot sample at that rate.
/// - read(out): the samples taken since the last read, oldest first, in
///   mV at the sampling point; returns how many (up to out.size()).
/// - lostSamples(): samples dropped since start because read() came too
///   late.
/// - stop(): stops sampling.
template <class T>
concept AudioSampler = requires(T &s, std::span<std::uint16_t> out) {
    { s.start(std::uint32_t{}) } -> std::same_as<bool>;
    { s.read(out) } -> std::same_as<std::size_t>;
    { s.lostSamples() } -> std::same_as<std::uint32_t>;
    s.stop();
};

} // namespace dmm::services
