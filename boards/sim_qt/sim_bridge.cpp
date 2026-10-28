// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "sim_bridge.h"

#include "dmm/os/os.h"

#include <QCoreApplication>
#include <QQmlEngine>

#include <algorithm>

QStringList SimBridge::keyNames() const
{
    QStringList names;
    for (unsigned i = 0; i < static_cast<unsigned>(dmm::services::Key::Count); ++i) {
        const std::string_view n = m_input.keyLabel(static_cast<dmm::services::Key>(i));
        names << QString::fromUtf8(n.data(), static_cast<qsizetype>(n.size()));
    }
    return names;
}

void SimBridge::setKey(int key, bool down)
{
    if (key >= 0 && key < static_cast<int>(dmm::services::Key::Count)) {
        m_input.setKey(static_cast<dmm::services::Key>(key), down);
    }
}

void SimBridge::turnKnob(int clicks)
{
    m_input.turn(clicks);
}

void SimBridge::setVolume(int value)
{
    m_input.setVolume(value);
}

void SimBridge::sendCommand(const QString &text)
{
    QMutexLocker guard(&m_inputLock);
    m_consoleInput += text.toUtf8() + '\n';
}

std::size_t SimBridge::readInput(std::span<char> buffer)
{
    QMutexLocker guard(&m_inputLock);
    const auto n = std::min(buffer.size(), static_cast<std::size_t>(m_consoleInput.size()));
    std::copy_n(m_consoleInput.constData(), n, buffer.data());
    m_consoleInput.remove(0, static_cast<qsizetype>(n));
    return n;
}

SimBridge::SimBridge(QObject *parent)
    : QObject(parent)
{
    // Drawing calls arrive by the hundred per screen; one queued repaint
    // covers all that came before it.
    m_display.setChangedCallback([this] {
        if (!m_displayUpdatePending.exchange(true)) {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_displayUpdatePending = false;
                    emit displayChanged();
                },
                Qt::QueuedConnection);
        }
    });
}

SimBridge *SimBridge::instance()
{
    static SimBridge *bridge = new SimBridge;
    return bridge;
}

SimBridge *SimBridge::create(QQmlEngine *, QJSEngine *)
{
    SimBridge *bridge = instance();
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    return bridge;
}

void SimBridge::set(Led led, bool on)
{
    QMetaObject::invokeMethod(this, [this, led, on] {
        bool &state = led == Led::Red ? m_red : led == Led::Green ? m_green : m_blue;
        if (state != on) {
            state = on;
            emit ledsChanged();
        } }, Qt::QueuedConnection);
}

void SimBridge::writeLine(std::string_view line)
{
    const QString text = QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
    QMetaObject::invokeMethod(this, [this, text] { emit logLine(text); }, Qt::QueuedConnection);
}

void SimBridge::restart()
{
    dmm::os::requestStop();
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
}
