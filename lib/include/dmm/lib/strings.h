// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace dmm::lib {

// std::string_view::starts_with() and ends_with() go through substr() and
// compare(), whose range checks call the library's exception helpers when
// the optimizer leaves them in (as at -O0). These never do.

constexpr bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && std::string_view(text.data(), prefix.size()) == prefix;
}

constexpr bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && std::string_view(text.data() + text.size() - suffix.size(), suffix.size()) == suffix;
}

/// Takes the first word off @p text: everything up to the first space,
/// and the space itself.
constexpr std::string_view takeWord(std::string_view &text)
{
    const std::size_t end = text.find(' ');
    // Not substr(): its range check would pull in exception code.
    const std::string_view word(text.data(), end == std::string_view::npos ? text.size() : end);
    text = end == std::string_view::npos ? std::string_view() : std::string_view(text.data() + end + 1, text.size() - end - 1);
    return word;
}

/// @p word as a number in @p base (10 or 16, lower-case hex digits), up to
/// eight digits; nothing if it is not one.
constexpr std::optional<std::int32_t> parseNumber(std::string_view word, int base)
{
    if (word.empty() || word.size() > 8) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    for (const char c : word) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                                              : -1;
        if (digit < 0) {
            return std::nullopt;
        }
        value = value * base + digit;
    }
    if (value > INT32_MAX) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}

} // namespace dmm::lib
