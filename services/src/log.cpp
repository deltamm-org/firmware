// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/log.h"

#include "dmm/lib/ring_buffer.h"
#include "dmm/os/os.h"

#include <atomic>

namespace dmm::services::log {

namespace {

// Lines are stored back to back, each ended by '\n'.
lib::RingBuffer<char, 4096> pending;
std::size_t droppedLines = 0;

// Constructed before main, so it exists before any task can log.
os::Mutex queueLock;

Tap tap = nullptr;
std::atomic<bool> queueDebug{true};

char levelLetter(Level level)
{
    switch (level) {
    case Level::Debug:
        return 'D';
    case Level::Info:
        return 'I';
    case Level::Warning:
        return 'W';
    case Level::Error:
        return 'E';
    }
    return '?';
}

} // namespace

void setTap(Tap t)
{
    tap = t;
}

void setDebugQueued(bool on)
{
    queueDebug.store(on, std::memory_order_relaxed);
}

bool debugQueued()
{
    return queueDebug.load(std::memory_order_relaxed);
}

Line::Line(Level level, std::string_view module)
    : m_level(level)
{
    const os::Ticks t = os::nowMs();
    const os::Ticks ms = t % 1000;
    m_text << '[' << (t / 1000) << '.';
    m_text << static_cast<char>('0' + ms / 100) << static_cast<char>('0' + ms / 10 % 10)
           << static_cast<char>('0' + ms % 10) << "] " << levelLetter(level) << ' ' << module << ": ";
}

Line::~Line()
{
    const std::string_view text = m_text.view();
    queueLock.lock();
    if (m_level == Level::Debug && !queueDebug.load(std::memory_order_relaxed)) {
        // Not queued; the tap still sees it.
    } else if (pending.freeSpace() < text.size() + 1) {
        ++droppedLines;
    } else {
        for (char c : text) {
            pending.push(c == '\n' ? ' ' : c);
        }
        pending.push('\n');
    }
    queueLock.unlock();
    if (tap != nullptr) {
        tap(m_level, text);
    }
}

std::size_t drain(LogSink &sink)
{
    std::size_t written = 0;
    for (;;) {
        lib::StaticString<MaxLineLength> line;
        std::size_t dropped = 0;
        bool haveLine = false;
        queueLock.lock();
        while (!pending.empty()) {
            const char c = *pending.pop();
            if (c == '\n') {
                haveLine = true;
                break;
            }
            line << c;
        }
        if (!haveLine) {
            dropped = droppedLines;
            droppedLines = 0;
        }
        queueLock.unlock();

        if (haveLine) {
            sink.writeLine(line.view());
            ++written;
            continue;
        }
        if (dropped > 0) {
            lib::StaticString<MaxLineLength> note;
            note << "[log] " << dropped << " lines dropped";
            sink.writeLine(note.view());
            ++written;
        }
        return written;
    }
}

} // namespace dmm::services::log
