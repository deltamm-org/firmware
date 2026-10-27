// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"

#include <concepts>
#include <cstdint>
#include <span>

namespace dmm::services {

/// The radio's own PC programming protocol, served on the data port next
/// to DeltaMM's protocol; the board names it (dmm/board/parts.h).
///
/// - feed(bytes, now, write): takes received bytes, replies through
///   @p write. Outside program mode it only watches for the command that
///   enters it.
/// - active(now): in program mode, where every byte on the port is this
///   protocol's.
/// - takeCodeplugChanged(): true once after the PC wrote the codeplug; the
///   radio then starts again to use it.
template <typename T>
concept Programming = requires(T &p, std::span<const std::uint8_t> bytes, os::Ticks now,
    lib::FnRef<void(std::span<const std::uint8_t>)> write) {
    p.feed(bytes, now, write);
    { p.active(now) } -> std::same_as<bool>;
    { p.takeCodeplugChanged() } -> std::same_as<bool>;
};

} // namespace dmm::services
