// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"
#include "dmm/services/storage.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dmm::services {

/// DeltaMM's own PC protocol, on the data port.
///
/// Every request starts with 0xDD; bytes before it are skipped.
///   0xDD 'i'                          information
///   0xDD 'r' address:u32 length:u16   read storage, remapped
///   0xDD 'p' address:u32 length:u16   read storage without bad-block remapping
/// Every reply is 0xDD, the request's letter, payload length:u16, the
/// payload, a status byte and a CRC-32 of everything before it. Numbers
/// are little-endian. Status: 0 ok, 1 corrected, 2 flagged by the chip,
/// 3 timeout, 4 no chip, 5 out of range, 0xFE bad request.
class DataLink
{
public:
    static constexpr std::uint8_t Start = 0xDD;
    static constexpr std::size_t MaxReadLength = 4096;
    static constexpr std::uint8_t BadRequest = 0xFE;
    /// A request not completed in this time is dropped.
    static constexpr os::Ticks RequestTimeoutMs = 1000;

    using Writer = lib::FnRef<void(std::span<const std::uint8_t>)>;

    /// @p version goes into the information reply. Without @p storage,
    /// reads answer "no chip".
    DataLink(Storage *storage, std::string_view version);

    /// Feeds received bytes; replies go to @p write.
    void feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write);

private:
    void handle(Writer write);

    Storage *m_storage;
    std::string_view m_version;
    std::uint8_t m_request[8]{};
    std::size_t m_have = 0;
    os::Ticks m_startedAt = 0;
};

} // namespace dmm::services
