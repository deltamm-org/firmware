// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/services/boot_loader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dmm::services::boot {

/// The loader's PC protocol, framed like the app's data protocol: every
/// request starts with 0xDD and a letter (bytes before 0xDD are skipped);
/// numbers are little-endian.
///   'i'                                     information
///   'e' length:u32                          erase slot B (0 = all of it)
///   'w' offset:u32 length:u16 data crc:u32  program into slot B
///   'k' offset:u32 length:u32               CRC-32 of a range of slot B
///   'c' length:u32 crc:u32 mode:u8          commit slot B's image
///   'b' mode:u8 argument:u16                restart: 0 boot, 1 roll back,
///                                           2 test swap cut after argument steps
/// Every reply is 0xDD, the letter, payload length:u16, the payload, a
/// status byte (LoaderStatus) and a CRC-32 of everything before it.
class LoaderProtocol
{
public:
    static constexpr std::uint8_t Start = 0xDD;
    static constexpr std::uint8_t Version = 1;
    static constexpr std::size_t MaxWrite = 4096;
    /// A request not completed in this time is dropped (milliseconds).
    static constexpr std::uint32_t RequestTimeoutMs = 1000;

    using Writer = lib::FnRef<void(std::span<const std::uint8_t>)>;

    /// What the PC asked for with 'b'.
    struct Restart
    {
        bool wanted = false;
        Request request = Request::None;
        std::uint16_t swapCut = 0;
    };

    LoaderProtocol(Loader &loader, std::string_view model, std::string_view version);

    /// The reason loader mode was entered, for the information reply.
    void setStayReason(StayReason reason) { m_stay = reason; }

    /// Feeds received bytes; replies go to @p write. @p nowMs is any
    /// millisecond clock.
    void feed(std::span<const std::uint8_t> bytes, std::uint32_t nowMs, Writer write);

    /// Set after a 'b' reply was sent: restart once it has gone out.
    const Restart &restart() const { return m_restart; }

private:
    std::size_t expectedLength() const;
    void handle(Writer write);
    void info(Writer write);

    Loader &m_loader;
    std::string_view m_model;
    std::string_view m_version;
    StayReason m_stay = StayReason::None;
    Restart m_restart;
    std::uint8_t m_request[2 + 6 + MaxWrite + 4]{};
    std::size_t m_have = 0;
    std::uint32_t m_startedAt = 0;
};

enum class LoaderStatus : std::uint8_t
{
    Ok = 0,
    /// The data's CRC-32 does not match.
    BadCrc = 1,
    /// Outside slot B, or a length the request does not allow.
    OutOfRange = 2,
    /// Erase or program failed (flash not erased, or a fault).
    FlashError = 3,
    /// Read back differs from what was written.
    VerifyFailed = 4,
    /// Not now: slot B holds the only confirmed image, or is not erased.
    Refused = 5,
    /// The image fails the commit checks; the payload names the problem.
    ImageRejected = 6,
    BadRequest = 0xFE,
};

} // namespace dmm::services::boot
