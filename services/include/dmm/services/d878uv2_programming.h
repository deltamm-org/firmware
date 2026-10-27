// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// The D878UVII's PC programming protocol, as codeplug tools speak it on
/// the radio's serial port, so far for reading:
///   "PROGRAM"                       enters program mode; answer "QX" 06
///   02                              identify; answer 'I', the model (7
///                                   bytes), the band code, the version
///                                   (6 bytes), 06
///   'R' address:u32 length:u8       read; answer 'W', the address, the
///                                   length, the data, a sum, 06
///   'W' address:u32 length:u8 data sum 06
///                                   write; answer 06
///   "END"                           leaves program mode; answer 06
/// Addresses are big-endian codeplug addresses. The sum is the low byte of
/// the address, length and data bytes added up. A write is refused (answer
/// 15) until writing exists.
class D878Uv2Programming
{
public:
    /// Reads @p data from codeplug address @p address; false on failure.
    using Reader = lib::FnRef<bool(std::uint32_t, std::span<std::uint8_t>)>;
    using Writer = lib::FnRef<void(std::span<const std::uint8_t>)>;

    static constexpr std::size_t MaxRead = 64;
    /// Program mode ends after this long without a command, as when the
    /// program on the PC went away without "END".
    static constexpr os::Ticks IdleMs = 10'000;
    /// A command not completed in this time is dropped.
    static constexpr os::Ticks CommandTimeoutMs = 1000;

    explicit D878Uv2Programming(Reader read)
        : m_read(read)
    {
    }

    /// Takes received bytes; replies go to @p write. In program mode all
    /// bytes are this protocol's; outside it, only "PROGRAM" is.
    void feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write);

    /// In program mode, where the port carries only this protocol.
    bool active(os::Ticks now);

private:
    void command(Writer write);

    Reader m_read;
    bool m_program = false;
    std::size_t m_matched = 0; // of "PROGRAM", outside program mode
    std::uint8_t m_command[24]{};
    std::size_t m_have = 0;
    os::Ticks m_lastAt = 0;
};

} // namespace dmm::services
