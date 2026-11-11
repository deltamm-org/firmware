// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "deltamm_client.h"

#include <QtEndian>

#include "zlib_crc.h"

namespace {

constexpr char Start = static_cast<char>(0xDD);
constexpr quint8 BadRequest = 0xFE;

} // namespace

DeltaMmClient::DeltaMmClient(Transport &transport)
    : m_transport(transport)
{
}

bool dmmExchange(Transport &transport, const QByteArray &request, QByteArray &payload, quint8 &status, QString *error,
    int timeoutMs)
{
    transport.discardInput();
    if (!transport.write(request)) {
        *error = QStringLiteral("the data port is gone");
        return false;
    }
    const QByteArray header = transport.read(4, timeoutMs);
    if (header.size() < 4 || header.at(0) != Start || header.at(1) != request.at(1)) {
        *error = header.isEmpty() ? QStringLiteral("no reply (is this the DeltaMM data port?)") : QStringLiteral("garbled reply");
        return false;
    }
    const auto length = qFromLittleEndian<quint16>(header.constData() + 2);
    const QByteArray rest = transport.read(length + 5, timeoutMs + length / 50);
    if (rest.size() < length + 5) {
        *error = QStringLiteral("reply cut short");
        return false;
    }
    const QByteArray body = header + rest.left(length + 1);
    const quint32 crc = qFromLittleEndian<quint32>(rest.constData() + length + 1);
    if (dmmCrc32(body) != crc) {
        *error = QStringLiteral("reply checksum wrong");
        return false;
    }
    payload = rest.left(length);
    status = static_cast<quint8>(rest.at(length));
    return true;
}

std::optional<DeltaMmInfo> DeltaMmClient::info(QString *error, int timeoutMs)
{
    QByteArray payload;
    quint8 status = 0;
    if (!exchange(QByteArray("\xDD"
                             "i",
                      2),
            payload, status, error, timeoutMs)) {
        return std::nullopt;
    }
    if (status != 0 || payload.size() < 10 || payload.size() < 10 + static_cast<quint8>(payload.at(9))) {
        *error = QStringLiteral("unexpected information reply");
        return std::nullopt;
    }
    DeltaMmInfo i;
    i.storagePresent = payload.at(0) != 0;
    i.maker = static_cast<quint8>(payload.at(1));
    i.device = static_cast<quint8>(payload.at(2));
    i.storageBytes = qFromLittleEndian<quint32>(payload.constData() + 3);
    i.remappedBlocks = qFromLittleEndian<qint16>(payload.constData() + 7);
    const int versionLength = static_cast<quint8>(payload.at(9));
    i.version = QString::fromLatin1(payload.mid(10, versionLength));
    const QByteArray loader = payload.mid(10 + versionLength);
    if (loader.size() >= 7 && loader.at(0) != 0) {
        i.loader.present = true;
        i.loader.trial = loader.at(1) != 0;
        i.loader.manual = loader.at(2) != 0;
        i.loader.attempts = static_cast<quint8>(loader.at(3));
        i.loader.maxAttempts = static_cast<quint8>(loader.at(4));
        i.loader.previousKept = loader.at(5) != 0;
        i.loader.fallback = static_cast<quint8>(loader.at(6));
    }
    return i;
}

bool DeltaMmClient::enterLoader(bool rollback, QString *error)
{
    QByteArray request("\xDD"
                       "B\x01",
        3);
    if (rollback) {
        request[2] = 2;
    }
    QByteArray payload;
    quint8 status = 0;
    if (!exchange(request, payload, status, error, 1000)) {
        return false;
    }
    if (status != 0) {
        *error = status == BadRequest ? QStringLiteral("this DeltaMM version cannot restart into the loader")
                                      : QStringLiteral("the radio refused: no loader installed, or busy (transmitting, writing the codeplug)");
        return false;
    }
    return true;
}

bool DeltaMmClient::confirm(QString *error)
{
    QByteArray payload;
    quint8 status = 0;
    if (!exchange(QByteArray("\xDD"
                             "K",
                      2),
            payload, status, error, 2000)) {
        return false;
    }
    if (status != 0) {
        *error = status == BadRequest ? QStringLiteral("this DeltaMM version has no confirmation")
                                      : QStringLiteral("the radio did not confirm (no loader installed?)");
        return false;
    }
    return true;
}

bool DeltaMmClient::read(quint32 address, quint32 length, bool physical, QByteArray &out, ReadReport &report, QString *error,
    const Progress &progress)
{
    out.clear();
    out.reserve(static_cast<qsizetype>(length));
    for (quint32 done = 0; done < length;) {
        const quint32 n = qMin<quint32>(MaxChunk, length - done);
        QByteArray request(8, '\0');
        request[0] = Start;
        request[1] = physical ? 'p' : 'r';
        qToLittleEndian(address + done, request.data() + 2);
        qToLittleEndian(static_cast<quint16>(n), request.data() + 6);
        QByteArray payload;
        quint8 status = 0;
        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
            ok = exchange(request, payload, status, error, 2000);
        }
        if (!ok) {
            *error = QStringLiteral("at 0x%1: %2").arg(address + done, 8, 16, QLatin1Char('0')).arg(*error);
            return false;
        }
        if (status == BadRequest || payload.size() != static_cast<qsizetype>(n)) {
            *error = QStringLiteral("the radio refused a read at 0x%1").arg(address + done, 8, 16, QLatin1Char('0'));
            return false;
        }
        if (status == 1) {
            ++report.correctedChunks;
        } else if (status == 2) {
            report.flaggedChunks.append(address + done);
        } else if (status != 0) {
            report.badChunks.append(address + done);
        }
        out += payload;
        done += n;
        if (progress) {
            progress(done, length);
        }
    }
    return true;
}
