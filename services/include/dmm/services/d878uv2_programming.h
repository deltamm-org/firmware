// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/os/os.h"
#include "dmm/services/d878uv2_codeplug.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// The D878UVII's PC programming protocol, as codeplug tools speak it on
/// the radio's serial port:
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
/// the address, length and data bytes added up. A write the codeplug
/// writer refuses is answered 15. "END" completes the writing; program
/// mode ending without it drops the block in progress.
class D878Uv2Programming
{
public:
    using Writer = lib::FnRef<void(std::span<const std::uint8_t>)>;

    static constexpr std::size_t MaxRead = 64;
    /// Program mode ends after this long without a command, as when the
    /// program on the PC went away without "END".
    static constexpr os::Ticks IdleMs = 10'000;
    /// A command not completed in this time is dropped.
    static constexpr os::Ticks CommandTimeoutMs = 1000;

    D878Uv2Programming(D878Uv2Codeplug &codeplug, D878Uv2CodeplugWriter &writer)
        : m_codeplug(codeplug), m_writer(writer)
    {
    }

    /// Takes received bytes; replies go to @p write. In program mode all
    /// bytes are this protocol's; outside it, only "PROGRAM" is.
    void feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write);

    /// In program mode, where the port carries only this protocol.
    bool active(os::Ticks now);

    /// True once after a program mode in which the codeplug was written:
    /// the radio should start again with it.
    bool takeCodeplugChanged()
    {
        const bool changed = m_changed;
        m_changed = false;
        return changed;
    }

private:
    void command(Writer write);
    void leave();

    D878Uv2Codeplug &m_codeplug;
    D878Uv2CodeplugWriter &m_writer;
    bool m_program = false;
    bool m_changed = false;
    std::size_t m_matched = 0; // of "PROGRAM", outside program mode
    std::uint8_t m_command[24]{};
    std::size_t m_have = 0;
    os::Ticks m_lastAt = 0;
};

} // namespace dmm::services
