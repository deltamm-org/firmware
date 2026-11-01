// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/os/os.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <cstring>
#include <vector>

namespace dmm::os {

namespace {

QElapsedTimer &elapsedClock()
{
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer;
}

std::atomic<bool> stopping{false};

QMutex registryLock;
std::vector<QThread *> &registry()
{
    static std::vector<QThread *> threads;
    return threads;
}

struct QueueSync
{
    QMutex lock;
    QWaitCondition notEmpty;
    QWaitCondition notFull;
};

QDeadlineTimer deadline(Ticks timeoutMs)
{
    return timeoutMs == WaitForever ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(static_cast<qint64>(timeoutMs));
}

} // namespace

Ticks nowMs()
{
    return static_cast<Ticks>(elapsedClock().elapsed());
}

void sleepMs(Ticks ms)
{
    QThread::msleep(ms);
}

void sleepUntil(Ticks &wake, Ticks period)
{
    wake += period;
    const auto remaining = static_cast<std::int32_t>(wake - nowMs());
    if (remaining > 0) {
        QThread::msleep(static_cast<unsigned long>(remaining));
    }
}

void runScheduler()
{
    elapsedClock();
}

bool stopRequested()
{
    return stopping.load();
}

void requestStop()
{
    stopping.store(true);
}

void joinAll()
{
    QMutexLocker guard(&registryLock);
    for (QThread *t : registry()) {
        t->wait();
        delete t;
    }
    registry().clear();
}

const char *currentTaskName()
{
    return nullptr;
}

void setFatalHandler(FatalHandler) {}

void setTickHook(TickHook) {}

TaskStack taskStack(std::size_t)
{
    return {};
}

namespace detail {

void startTask(TaskControl &t, const char *name, unsigned, void (*entry)(void *), void *arg, StackWord *, std::size_t)
{
    QThread *thread = QThread::create(entry, arg);
    thread->setObjectName(QString::fromUtf8(name));
    t.thread = thread;
    {
        QMutexLocker guard(&registryLock);
        registry().push_back(thread);
    }
    thread->start();
}

std::size_t unusedStackWords(TaskControl &)
{
    return 0;
}

void createQueue(QueueControl &q, std::uint8_t *storage, std::size_t itemSize, std::size_t capacity)
{
    q.impl = new QueueSync;
    q.storage = storage;
    q.itemSize = itemSize;
    q.capacity = capacity;
}

bool queueSend(QueueControl &q, const void *item, std::uint32_t timeoutMs)
{
    auto *s = static_cast<QueueSync *>(q.impl);
    QMutexLocker guard(&s->lock);
    const QDeadlineTimer until = deadline(timeoutMs);
    while (q.count == q.capacity) {
        if (!s->notFull.wait(&s->lock, until)) {
            return false;
        }
    }
    std::memcpy(q.storage + ((q.head + q.count) % q.capacity) * q.itemSize, item, q.itemSize);
    ++q.count;
    s->notEmpty.wakeOne();
    return true;
}

bool queueReceive(QueueControl &q, void *item, std::uint32_t timeoutMs)
{
    auto *s = static_cast<QueueSync *>(q.impl);
    QMutexLocker guard(&s->lock);
    const QDeadlineTimer until = deadline(timeoutMs);
    while (q.count == 0) {
        if (!s->notEmpty.wait(&s->lock, until)) {
            return false;
        }
    }
    std::memcpy(item, q.storage + q.head * q.itemSize, q.itemSize);
    q.head = (q.head + 1) % q.capacity;
    --q.count;
    s->notFull.wakeOne();
    return true;
}

void createMutex(MutexControl &m)
{
    m.impl = new QMutex;
}

void mutexLock(MutexControl &m)
{
    static_cast<QMutex *>(m.impl)->lock();
}

void mutexUnlock(MutexControl &m)
{
    static_cast<QMutex *>(m.impl)->unlock();
}

} // namespace detail

} // namespace dmm::os
