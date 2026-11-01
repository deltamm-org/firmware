// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace dmm::lib {

/// Hexadecimal formatting request for TextWriter, zero-padded to @c digits.
struct Hex
{
    std::uint32_t value;
    std::uint8_t digits;
};

constexpr Hex hex(std::uint32_t value, std::uint8_t digits = 8)
{
    return {value, digits};
}

/// Decimal formatting request for TextWriter, zero-padded to at least
/// @c digits.
struct ZeroPadded
{
    std::uint32_t value;
    std::uint8_t digits;
};

constexpr ZeroPadded zeroPadded(std::uint32_t value, std::uint8_t digits)
{
    return {value, digits};
}

/// Appends text and numbers to a caller-provided buffer, without printf
/// and without the heap. Output past the buffer is dropped and remembered.
class TextWriter
{
public:
    explicit TextWriter(std::span<char> buffer)
        : m_buf(buffer)
    {
    }

    TextWriter &operator<<(std::string_view s)
    {
        for (char c : s) {
            put(c);
        }
        return *this;
    }

    TextWriter &operator<<(const char *s) { return *this << std::string_view(s); }

    TextWriter &operator<<(char c)
    {
        put(c);
        return *this;
    }

    TextWriter &operator<<(bool b) { return *this << (b ? std::string_view("true") : std::string_view("false")); }

    template <std::integral I>
        requires(!std::same_as<I, char> && !std::same_as<I, bool>)
    TextWriter &operator<<(I value)
    {
        using U = std::make_unsigned_t<I>;
        auto magnitude = static_cast<U>(value);
        if constexpr (std::is_signed_v<I>) {
            if (value < 0) {
                put('-');
                magnitude = static_cast<U>(U{0} - magnitude);
            }
        }
        putDecimal(magnitude, 1);
        return *this;
    }

    TextWriter &operator<<(Hex h)
    {
        static constexpr char digits[] = "0123456789abcdef";
        for (int i = h.digits - 1; i >= 0; --i) {
            put(digits[(h.value >> (4 * i)) & 0xFu]);
        }
        return *this;
    }

    TextWriter &operator<<(ZeroPadded z)
    {
        putDecimal(z.value, z.digits);
        return *this;
    }

    std::string_view view() const { return {m_buf.data(), m_len}; }
    std::size_t size() const { return m_len; }
    bool truncated() const { return m_truncated; }

private:
    void put(char c)
    {
        if (m_len < m_buf.size()) {
            m_buf[m_len++] = c;
        } else {
            m_truncated = true;
        }
    }

    // Digits are made here rather than with std::to_chars, whose digit
    // table is copied onto the stack.
    template <std::unsigned_integral U>
    void putDecimal(U value, std::size_t minDigits)
    {
        char digits[std::numeric_limits<U>::digits10 + 1];
        std::size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        for (std::size_t i = n; i < minDigits; ++i) {
            put('0');
        }
        while (n > 0) {
            put(digits[--n]);
        }
    }

    std::span<char> m_buf;
    std::size_t m_len = 0;
    bool m_truncated = false;
};

/// Fixed-capacity string that accepts everything TextWriter does.
template <std::size_t N>
class StaticString
{
public:
    StaticString() = default;
    StaticString(std::string_view s) { *this << s; } // NOLINT(google-explicit-constructor)

    template <typename T>
    StaticString &operator<<(const T &value)
    {
        TextWriter w({m_data + m_len, N - m_len});
        w << value;
        m_len += w.size();
        m_truncated = m_truncated || w.truncated();
        return *this;
    }

    void clear()
    {
        m_len = 0;
        m_truncated = false;
    }

    std::string_view view() const { return {m_data, m_len}; }
    std::size_t size() const { return m_len; }
    static constexpr std::size_t capacity() { return N; }
    bool truncated() const { return m_truncated; }

private:
    char m_data[N]{};
    std::size_t m_len = 0;
    bool m_truncated = false;
};

} // namespace dmm::lib
