// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"
#include "dmm/services/storage.h"
#include "dmm/services/system_control.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dmm::services {

/// DeltaMM's own PC protocol, on the data port.
///
/// Every request starts with 0xDD; bytes before it are skipped. The data
/// port may also carry the radio's own programming protocol, so the start
/// byte must be one that protocol never begins a command with.
///   0xDD 'i'                          information
///   0xDD 'r' address:u32 length:u16   read storage, remapped
///   0xDD 'p' address:u32 length:u16   read storage without bad-block remapping
///   0xDD 'B' request:u8               restart into the loader: 1 to wait for
///                                     the PC, 2 to bring the previous image back
///   0xDD 'K'                          confirm the running image
/// Every reply is 0xDD, the request's letter, payload length:u16, the
/// payload, a status byte and a CRC-32 of everything before it. Numbers
/// are little-endian. Status: 0 ok, 1 corrected, 2 flagged by the chip,
/// 3 timeout, 4 no chip, 5 out of range, 6 refused (no loader, or busy),
/// 0xFE bad request. With a SystemControl, the information reply ends with
/// the loader status: present, trial, manual, attempts, allowed attempts,
/// previous image kept, last fallback (one byte each).
class DataLink
{
public:
    static constexpr std::uint8_t Start = 0xDD;
    static constexpr std::size_t MaxReadLength = 4096;
    static constexpr std::uint8_t Refused = 6;
    static constexpr std::uint8_t BadRequest = 0xFE;
    /// A request not completed in this time is dropped.
    static constexpr os::Ticks RequestTimeoutMs = 1000;

    using Writer = lib::FnRef<void(std::span<const std::uint8_t>)>;

    /// @p version goes into the information reply. Without @p storage,
    /// reads answer "no chip"; without @p system, loader requests are
    /// refused.
    DataLink(Storage *storage, std::string_view version, SystemControl *system = nullptr);

    /// Feeds received bytes; replies go to @p write.
    void feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write);

    /// While busy (transmitting, writing the codeplug), requests to restart
    /// into the loader are refused.
    void setBusy(bool busy) { m_busy = busy; }

    /// A restart into the loader that was accepted; restart once the reply
    /// has gone out.
    std::optional<LoaderRequest> takeLoaderRequest();

private:
    void handle(Writer write);

    Storage *m_storage;
    std::string_view m_version;
    SystemControl *m_system;
    bool m_busy = false;
    std::optional<LoaderRequest> m_loaderRequest;
    std::uint8_t m_request[8]{};
    std::size_t m_have = 0;
    os::Ticks m_startedAt = 0;
};

} // namespace dmm::services
