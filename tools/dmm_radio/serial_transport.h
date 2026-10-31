// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "transport.h"

#include <QList>
#include <QSerialPort>
#include <QSerialPortInfo>

/// A radio's USB serial port.
class SerialTransport final : public Transport
{
public:
    /// Serial ports whose USB IDs match the radios dmm-radio supports.
    static QList<QSerialPortInfo> findRadios();

    /// Serial ports of radios running DeltaMM (data and log ports alike).
    static QList<QSerialPortInfo> findDeltaMmPorts();

    bool open(const QString &portName, QString *error);
    void close();
    bool write(const QByteArray &data) override;
    QByteArray read(qsizetype count, int timeoutMs) override;
    void discardInput() override;

private:
    QSerialPort m_port;
};
