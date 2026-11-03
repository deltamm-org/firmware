// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "transport.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

/// One slot as the loader reports it.
struct LoaderSlot
{
    /// 0 empty, 1 confirmed, 2 trial, 3 failed, 4 committed (boot record).
    int state = 0;
    bool manual = false;
    quint32 length = 0;
    quint32 crc = 0;
    /// From the image's info block; empty when unknown.
    QString version;
};

/// What DeltaMM's loader reports in loader mode.
struct LoaderInfo
{
    int protocol = 0;
    QString model;
    QString version;
    quint32 slotA = 0;
    quint32 slotASize = 0;
    quint32 slotB = 0;
    quint32 slotBSize = 0;
    LoaderSlot a;
    LoaderSlot b;
    int attempts = 0;
    int maxAttempts = 0;
    int fallback = 0;
    bool swapActive = false;
    /// Why it waits: 1 asked to, 2 force key, 3 nothing to start, 4 flash error.
    int stayReason = 0;
};

QString slotStateName(int state);
QString fallbackName(int fallback);
QString stayReasonName(int reason);

/// What answers on a port in DeltaMM's framing.
enum class DmmPortKind
{
    None,
    App,
    Loader,
};

/// Asks with an information request whether the app or the loader is on
/// @p transport.
DmmPortKind probeDmmPort(Transport &transport, int timeoutMs = 400);

/// Client for the loader's protocol (services/loader_protocol.h).
class LoaderClient
{
public:
    static constexpr int ChunkBytes = 4096;
    using Progress = std::function<void(qint64 done, qint64 total)>;

    explicit LoaderClient(Transport &transport);

    std::optional<LoaderInfo> info(QString *error, int timeoutMs = 1000);
    /// Erases the first @p length bytes of slot B.
    bool erase(quint32 length, QString *error);
    /// Programs @p data at @p offset in slot B. @p status gets the
    /// loader's status byte, or -1 when there was no valid reply.
    bool write(quint32 offset, const QByteArray &data, QString *error, int *status = nullptr, bool badCrc = false);
    std::optional<quint32> checksum(quint32 offset, quint32 length, QString *error);
    /// Commits the image in slot B. @p problem gets the loader's reason
    /// code when it rejects the image.
    bool commit(quint32 length, quint32 crc, bool manual, QString *error, int *problem = nullptr);
    /// Restarts: mode 0 boot, 1 roll back, 2 test swap cut after
    /// @p argument steps.
    bool restart(int mode, quint16 argument, QString *error);

    struct UploadReport
    {
        int skippedChunks = 0;
        int writtenChunks = 0;
        int retries = 0;
        bool erased = false;
    };

    /// Puts @p image into slot B, picking up what an earlier, broken
    /// upload already wrote when slot B is otherwise erased; then commits
    /// it. @p corruptChunk (tests) sends that chunk once with a wrong CRC.
    bool upload(const QByteArray &image, bool manual, UploadReport &report, QString *error, const Progress &progress = {},
        int corruptChunk = -1);

    static std::optional<LoaderInfo> parseInfo(const QByteArray &payload);

private:
    bool simple(const QByteArray &request, QString *error, int timeoutMs, int *status = nullptr, QByteArray *payload = nullptr);

    Transport &m_transport;
};
