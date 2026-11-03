// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "transport.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

/// One request in DeltaMM's framing (0xDD, letter, ...) and its reply:
/// checks the reply's letter, length and CRC and returns its payload and
/// status. Used for the app's data protocol and the loader's.
bool dmmExchange(Transport &transport, const QByteArray &request, QByteArray &payload, quint8 &status, QString *error,
    int timeoutMs);

/// How the loader sees the running image (radios with DeltaMM's loader).
struct DeltaMmLoaderState
{
    bool present = false;
    bool trial = false;
    bool manual = false;
    int attempts = 0;
    int maxAttempts = 0;
    bool previousKept = false;
    /// The loader's fallback code: 0 none, 1 not confirmed, 2 check
    /// failed, 3 asked for, 4 no image.
    int fallback = 0;
};

/// What a radio running DeltaMM reports about itself and its data flash.
struct DeltaMmInfo
{
    QString version;
    bool storagePresent = false;
    quint8 maker = 0;
    quint8 device = 0;
    quint32 storageBytes = 0;
    /// -1: the radio found no bad-block table.
    int remappedBlocks = -1;
    /// Empty (present false) from images that report no loader state.
    DeltaMmLoaderState loader;
};

/// Client for DeltaMM's data protocol on the radio's data port.
class DeltaMmClient
{
public:
    static constexpr int MaxChunk = 4096;
    using Progress = std::function<void(qint64 done, qint64 total)>;

    explicit DeltaMmClient(Transport &transport);

    std::optional<DeltaMmInfo> info(QString *error, int timeoutMs = 1000);

    struct ReadReport
    {
        int correctedChunks = 0;
        /// Start addresses of chunks the chip flagged as uncorrectable. The
        /// data is what the chip returned and may be wrong.
        QList<quint32> flaggedChunks;
        /// Start addresses of chunks that could not be read (sent as 0xFF).
        QList<quint32> badChunks;
    };

    /// Reads @p length bytes from @p address, logical (remapped) or
    /// physical. False with @p error set when the link fails.
    bool read(quint32 address, quint32 length, bool physical, QByteArray &out, ReadReport &report, QString *error,
        const Progress &progress = {});

    /// Asks the image to restart into the loader: to wait for the PC, or
    /// to bring the previous image back.
    bool enterLoader(bool rollback, QString *error);

    /// Confirms the running image.
    bool confirm(QString *error);

private:
    bool exchange(const QByteArray &request, QByteArray &payload, quint8 &status, QString *error, int timeoutMs)
    {
        return dmmExchange(m_transport, request, payload, status, error, timeoutMs);
    }

    Transport &m_transport;
};
