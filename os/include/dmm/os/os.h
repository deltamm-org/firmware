// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// Task, queue and mutex API shared by every board. The backend (FreeRTOS on
// radios, Qt threads on the PC) provides dmm/os/detail/backend.h.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dmm/os/detail/backend.h"

namespace dmm::os {

/// Milliseconds since start, wrapping after about 49 days.
using Ticks = std::uint32_t;

/// Blocks until a message arrives, without a time limit.
inline constexpr Ticks WaitForever = 0xFFFFFFFFu;

Ticks nowMs();
void sleepMs(Ticks ms);

/// Sleeps until @p wake + @p period, then advances @p wake by @p period.
/// Keeps a periodic loop on its own schedule regardless of its run time.
void sleepUntil(Ticks &wake, Ticks period);

/// Starts the scheduler. On a radio it never returns. On the PC tasks
/// already run as threads and it returns at once.
void runScheduler();

/// Asks host tasks to finish (the simulator's quit path). Always false on a radio.
bool stopRequested();
void requestStop();

/// Waits for every host task to return. No-op on a radio.
void joinAll();

/// Name of the running task, or nullptr outside one (or on the PC).
const char *currentTaskName();

/// Called when the kernel finds a fatal error, such as a task overrunning
/// its stack. Must not return. Without one the CPU stops and the watchdog
/// restarts the radio.
using FatalHandler = void (*)(const char *what, const char *task);
void setFatalHandler(FatalHandler handler);

/// Called from the kernel's tick interrupt every millisecond, in whatever
/// task that interrupt preempted. Must be short and must not block. Never
/// called on the PC.
using TickHook = void (*)();
void setTickHook(TickHook hook);

/// A started task's name and the stack words it has never used.
struct TaskStack
{
    const char *name = nullptr;
    std::size_t unusedWords = 0;
};

/// The @p index-th started task, in start order. The name is nullptr past
/// the last task, and on the PC, where stacks are not measured.
TaskStack taskStack(std::size_t index);

/// A task with a statically allocated stack of @p StackWords 32-bit words.
/// Higher @p priority is more urgent (1..7).
template <std::size_t StackWords>
class Task
{
public:
    using EntryFn = void (*)(void *);

    /// Creates and starts the task. Call once.
    void start(const char *name, unsigned priority, EntryFn entry, void *arg)
    {
        detail::startTask(m_control, name, priority, entry, arg, m_stack, StackWords);
    }

    /// Stack words never used since the task started; 0 where unknown (PC).
    std::size_t unusedStackWords() { return detail::unusedStackWords(m_control); }

private:
    detail::TaskControl m_control{};
    detail::StackWord m_stack[detail::stackWords(StackWords)]{};
};

/// Fixed-size message queue of trivially copyable @p T.
template <typename T, std::size_t N>
class Queue
{
    static_assert(std::is_trivially_copyable_v<T>, "queue copies messages byte by byte");

public:
    Queue() { detail::createQueue(m_control, m_storage, sizeof(T), N); }

    /// Copies @p msg in. Waits up to @p timeoutMs for space; false on timeout.
    bool send(const T &msg, Ticks timeoutMs = 0)
    {
        return detail::queueSend(m_control, &msg, timeoutMs);
    }

    /// Copies the oldest message out. Waits up to @p timeoutMs; false on timeout.
    bool receive(T &msg, Ticks timeoutMs = WaitForever)
    {
        return detail::queueReceive(m_control, &msg, timeoutMs);
    }

private:
    detail::QueueControl m_control{};
    alignas(T) std::uint8_t m_storage[sizeof(T) * N]{};
};

/// Mutual exclusion between tasks (not interrupts). Meets BasicLockable.
class Mutex
{
public:
    Mutex() { detail::createMutex(m_control); }
    void lock() { detail::mutexLock(m_control); }
    void unlock() { detail::mutexUnlock(m_control); }

private:
    detail::MutexControl m_control{};
};

} // namespace dmm::os
