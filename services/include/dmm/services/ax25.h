// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fixed_vector.h"
#include "dmm/lib/format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

/// AX.25 UI frames, the packets APRS travels in: addresses, the info field
/// and the frame check sequence. Flags, bit stuffing and NRZI belong to the
/// modem.
namespace dmm::services::ax25 {

/// A station or digipeater address: a callsign of up to six letters and
/// digits, and an SSID from 0 to 15.
struct Address
{
    lib::StaticString<6> call;
    std::uint8_t ssid = 0;
    /// In a path: this digipeater has repeated the frame (the H bit).
    bool repeated = false;
};

/// "CALL", "CALL-7" or, for a repeated digipeater, "WIDE1-1*"; nothing if
/// the text is not an address.
std::optional<Address> parseAddress(std::string_view text);
/// The address as parseAddress() reads it.
lib::StaticString<10> addressText(const Address &address);

inline constexpr std::size_t MaxPath = 8;
inline constexpr std::size_t MaxInfo = 256;
/// Longest frame encode() writes: the addresses, control and protocol
/// bytes, the info field and the frame check sequence.
inline constexpr std::size_t MaxFrameBytes = 7 * (2 + MaxPath) + 2 + MaxInfo + 2;

/// A UI frame (control 0x03) with no layer 3 protocol (0xF0).
struct Frame
{
    Address destination;
    Address source;
    lib::FixedVector<Address, MaxPath> path;
    /// The info field; it may hold any byte.
    lib::StaticString<MaxInfo> info;
};

/// The frame check sequence: CRC-16/X.25 (reflected polynomial 0x8408,
/// start 0xFFFF, result inverted). It goes on air low byte first.
std::uint16_t frameCheck(std::span<const std::uint8_t> bytes);

/// Writes the frame from the first address to the frame check sequence;
/// returns its length, or 0 if @p out is too small.
std::size_t encode(const Frame &frame, std::span<std::uint8_t> out);

/// Reads a frame from the first address to the frame check sequence.
/// False if the check fails, the address field is malformed, or it is not
/// a UI frame without layer 3.
bool decode(std::span<const std::uint8_t> bytes, Frame &out);

/// Frames seen lately, to drop copies of one frame that arrive more than
/// once within a short time (from several decoders). Frames are told apart
/// by their length and frame check sequence.
class RecentFrames
{
public:
    /// True if a frame with the same length and check (its last two bytes)
    /// was seen within @p window before @p now (in any unit of time);
    /// otherwise remembers it.
    bool seen(std::span<const std::uint8_t> frame, std::uint32_t now, std::uint32_t window);

private:
    struct Entry
    {
        std::uint32_t at = 0;
        std::uint16_t check = 0;
        std::uint16_t length = 0;
    };

    std::array<Entry, 8> m_entries{};
    std::size_t m_next = 0;
};

/// "SOURCE>DEST,PATH1*,PATH2:info", the usual way to print a packet.
template <typename Writer>
void writeText(Writer &w, const Frame &frame)
{
    w << addressText(frame.source).view() << '>' << addressText(frame.destination).view();
    for (const Address &a : frame.path) {
        w << ',' << addressText(a).view();
    }
    w << ':' << frame.info.view();
}

} // namespace dmm::services::ax25
