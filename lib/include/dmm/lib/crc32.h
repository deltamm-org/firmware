// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

namespace dmm::lib {

namespace detail {

// The CRC of each 4-bit value, for working a nibble at a time.
inline constexpr std::uint32_t Crc32Nibbles[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};

} // namespace detail

/// CRC-32 as in zlib, PNG and Ethernet (reflected, polynomial 0xEDB88320).
/// Start with 0 and feed the previous result to continue over more data.
constexpr std::uint32_t crc32(const std::uint8_t *data, std::size_t size, std::uint32_t crc = 0)
{
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ detail::Crc32Nibbles[crc & 0xFu];
        crc = (crc >> 4) ^ detail::Crc32Nibbles[crc & 0xFu];
    }
    return ~crc;
}

} // namespace dmm::lib
