// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/lib/format.h"
#include "dmm/services/log_sink.h"

#include <cstddef>
#include <string_view>

namespace dmm::services::log {

enum class Level
{
    Debug,
    Info,
    Warning,
    Error,
};

/// Longest line kept, including the time stamp and module prefix.
inline constexpr std::size_t MaxLineLength = 160;

/// One log line under construction. Appended with <<, queued when the
/// object goes out of scope. Any task may log; interrupts may not.
class Line
{
public:
    Line(Level level, std::string_view module);
    ~Line();
    Line(const Line &) = delete;
    Line &operator=(const Line &) = delete;

    template <typename T>
    Line &operator<<(const T &value)
    {
        m_text << value;
        return *this;
    }

private:
    Level m_level;
    lib::StaticString<MaxLineLength> m_text;
};

inline Line debug(std::string_view module)
{
    return {Level::Debug, module};
}
inline Line info(std::string_view module)
{
    return {Level::Info, module};
}
inline Line warning(std::string_view module)
{
    return {Level::Warning, module};
}
inline Line error(std::string_view module)
{
    return {Level::Error, module};
}

/// Called with every finished line, in the logging task, after the line is
/// queued. Must not log. Set once at start.
using Tap = void (*)(Level level, std::string_view line);
void setTap(Tap tap);

/// Whether debug lines are queued for the sink (on from start). Off, the
/// queue keeps the other lines longer while nothing reads it.
void setDebugQueued(bool on);
bool debugQueued();

/// Moves every queued line to @p sink, oldest first. Lines that did not fit
/// the queue are reported as one summary line. Returns the lines written.
std::size_t drain(LogSink &sink);

} // namespace dmm::services::log
