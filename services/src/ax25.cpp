// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/ax25.h"

namespace dmm::services::ax25 {

namespace {

constexpr std::uint8_t UiControl = 0x03;
constexpr std::uint8_t NoLayer3 = 0xF0;
// Bits of an address's last byte, above the SSID (bits 1-4).
constexpr std::uint8_t LastAddress = 0x01;
constexpr std::uint8_t Reserved = 0x60;
constexpr std::uint8_t CommandOrRepeated = 0x80;

bool isCallChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Seven bytes: the callsign padded with spaces, each shifted left one bit,
// then the SSID byte.
void putAddress(std::uint8_t *out, const Address &a, std::uint8_t flags)
{
    const std::string_view call = a.call.view();
    for (std::size_t i = 0; i < 6; ++i) {
        out[i] = static_cast<std::uint8_t>((i < call.size() ? call[i] : ' ') << 1);
    }
    out[6] = static_cast<std::uint8_t>(Reserved | flags | ((a.ssid & 0x0F) << 1));
}

bool takeAddress(const std::uint8_t *in, Address &a)
{
    a.call.clear();
    bool ended = false;
    for (std::size_t i = 0; i < 6; ++i) {
        if ((in[i] & 1) != 0) {
            return false;
        }
        const char c = static_cast<char>(in[i] >> 1);
        if (c == ' ') {
            ended = true;
        } else if (ended || !isCallChar(c)) {
            return false;
        } else {
            a.call << c;
        }
    }
    a.ssid = static_cast<std::uint8_t>((in[6] >> 1) & 0x0F);
    a.repeated = (in[6] & CommandOrRepeated) != 0;
    return a.call.size() != 0;
}

} // namespace

std::optional<Address> parseAddress(std::string_view text)
{
    Address a;
    if (!text.empty() && text.back() == '*') {
        a.repeated = true;
        text.remove_suffix(1);
    }
    const std::size_t dash = text.find('-');
    const std::string_view call(text.data(), dash == std::string_view::npos ? text.size() : dash);
    if (call.empty() || call.size() > 6) {
        return std::nullopt;
    }
    for (char c : call) {
        if (!isCallChar(c)) {
            return std::nullopt;
        }
        a.call << c;
    }
    if (dash != std::string_view::npos) {
        const std::string_view ssid(text.data() + dash + 1, text.size() - dash - 1);
        if (ssid.empty() || ssid.size() > 2 || (ssid.size() == 2 && ssid[0] == '0')) {
            return std::nullopt;
        }
        unsigned value = 0;
        for (char c : ssid) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value > 15) {
            return std::nullopt;
        }
        a.ssid = static_cast<std::uint8_t>(value);
    }
    return a;
}

lib::StaticString<10> addressText(const Address &address)
{
    lib::StaticString<10> text;
    text << address.call.view();
    if (address.ssid != 0) {
        text << '-' << address.ssid;
    }
    if (address.repeated) {
        text << '*';
    }
    return text;
}

std::uint16_t frameCheck(std::span<const std::uint8_t> bytes)
{
    std::uint16_t crc = 0xFFFF;
    for (std::uint8_t b : bytes) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 1) != 0 ? static_cast<std::uint16_t>((crc >> 1) ^ 0x8408) : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return static_cast<std::uint16_t>(~crc);
}

std::size_t encode(const Frame &frame, std::span<std::uint8_t> out)
{
    const std::size_t length = 7 * (2 + frame.path.size()) + 2 + frame.info.size() + 2;
    if (out.size() < length) {
        return 0;
    }
    std::uint8_t *p = out.data();
    // A command frame: the destination's C bit set, the source's clear.
    putAddress(p, frame.destination, CommandOrRepeated);
    p += 7;
    putAddress(p, frame.source, frame.path.empty() ? LastAddress : 0);
    p += 7;
    for (std::size_t i = 0; i < frame.path.size(); ++i) {
        const Address &a = frame.path[i];
        putAddress(p, a, static_cast<std::uint8_t>((a.repeated ? CommandOrRepeated : 0) | (i + 1 == frame.path.size() ? LastAddress : 0)));
        p += 7;
    }
    *p++ = UiControl;
    *p++ = NoLayer3;
    for (char c : frame.info.view()) {
        *p++ = static_cast<std::uint8_t>(c);
    }
    const std::uint16_t check = frameCheck({out.data(), static_cast<std::size_t>(p - out.data())});
    *p++ = static_cast<std::uint8_t>(check & 0xFF);
    *p++ = static_cast<std::uint8_t>(check >> 8);
    return length;
}

bool decode(std::span<const std::uint8_t> bytes, Frame &out)
{
    // Two addresses, control, protocol and the check at least.
    if (bytes.size() < 7 * 2 + 2 + 2) {
        return false;
    }
    const std::size_t body = bytes.size() - 2;
    const std::uint16_t check = static_cast<std::uint16_t>(bytes[body] | (bytes[body + 1] << 8));
    if (frameCheck(bytes.first(body)) != check) {
        return false;
    }
    std::size_t pos = 0;
    std::size_t count = 0;
    out.path.clear();
    for (;;) {
        if (pos + 7 > body || count == 2 + MaxPath) {
            return false;
        }
        Address a;
        if (!takeAddress(&bytes[pos], a)) {
            return false;
        }
        const bool last = (bytes[pos + 6] & LastAddress) != 0;
        if (count == 0) {
            a.repeated = false;
            out.destination = a;
        } else if (count == 1) {
            a.repeated = false;
            out.source = a;
        } else {
            out.path.pushBack(a);
        }
        pos += 7;
        ++count;
        if (last) {
            break;
        }
    }
    if (count < 2 || pos + 2 > body || bytes[pos] != UiControl || bytes[pos + 1] != NoLayer3 || body - pos - 2 > MaxInfo) {
        return false;
    }
    out.info.clear();
    for (std::size_t i = pos + 2; i < body; ++i) {
        out.info << static_cast<char>(bytes[i]);
    }
    return true;
}

} // namespace dmm::services::ax25
