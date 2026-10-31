// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// A serial link to another chip or board: start() powers it as needed
/// and opens the link, stop() closes and powers it down; read() takes what
/// has arrived without waiting, write() sends.
template <typename T>
concept SerialLink = requires(T &link, const T &state, std::span<std::uint8_t> in, std::span<const std::uint8_t> out) {
    link.start();
    link.stop();
    { link.read(in) } -> std::same_as<std::size_t>;
    link.write(out);
    { state.lostBytes() } -> std::same_as<std::uint32_t>;
};

} // namespace dmm::services
