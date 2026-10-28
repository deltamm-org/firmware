// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/leds.h"
#include "dmm/services/log_sink.h"
#include "dmm/services/system_control.h"

#include "dmm/board/sim_battery.h"
#include "dmm/board/sim_input.h"
#include "sim_display.h"
#include "sim_storage.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QStringList>

#include <QtQml/qqmlregistration.h>
#include <atomic>

/// Connects the app's service interfaces to the QML window. Calls from app
/// tasks are queued onto the GUI thread.
class SimBridge : public QObject, public dmm::services::Leds, public dmm::services::LogSink, public dmm::services::SystemControl
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool redOn READ redOn NOTIFY ledsChanged)
    Q_PROPERTY(bool greenOn READ greenOn NOTIFY ledsChanged)
    Q_PROPERTY(bool blueOn READ blueOn NOTIFY ledsChanged)
    Q_PROPERTY(QStringList keyNames READ keyNames CONSTANT)

public:
    static SimBridge *instance();
    static SimBridge *create(class QQmlEngine *, class QJSEngine *);

    bool redOn() const
    {
        return m_red;
    }
    bool greenOn() const
    {
        return m_green;
    }
    bool blueOn() const
    {
        return m_blue;
    }

    SimDisplay &display()
    {
        return m_display;
    }
    SimInput &input()
    {
        return m_input;
    }
    SimBattery &battery()
    {
        return m_battery;
    }
    SimStorage &storage()
    {
        return m_storage;
    }
    Q_INVOKABLE void setBatteryMillivolts(int mv) { m_battery.setMillivolts(mv); }

    /// Key names in Key order, for the key buttons.
    QStringList keyNames() const;
    Q_INVOKABLE void setKey(int key, bool down);
    Q_INVOKABLE void turnKnob(int clicks);
    Q_INVOKABLE void setVolume(int value);

    /// Types @p text plus Enter on the app's debug console.
    Q_INVOKABLE void sendCommand(const QString &text);

    void set(Led led, bool on) override;
    void writeLine(std::string_view line) override;
    std::size_t readInput(std::span<char> buffer) override;

    // The window has no power switch or watchdog; closing it is power-off.
    bool powerSwitchOff() override
    {
        return false;
    }
    void holdPower() override {}
    void releasePower() override {}
    void feedWatchdog() override {}
    void restart() override;
    dmm::services::ResetReport lastReset() override
    {
        return {"simulator start", 0, false, {}};
    }
    void testFault() override { restart(); }

signals:
    void ledsChanged();
    void logLine(const QString &line);
    void displayChanged();

private:
    explicit SimBridge(QObject *parent = nullptr);

    bool m_red = false;
    bool m_green = false;
    bool m_blue = false;
    SimDisplay m_display;
    SimInput m_input;
    SimBattery m_battery;
    SimStorage m_storage;
    std::atomic<bool> m_displayUpdatePending{false};
    QMutex m_inputLock;
    QByteArray m_consoleInput;
};
