// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "loader_client.h"

#include "deltamm_client.h"
#include "zlib_crc.h"

#include <QtEndian>

namespace {

constexpr char Start = static_cast<char>(0xDD);
constexpr int StatusBadCrc = 1;
constexpr int StatusFlashError = 3;
constexpr int StatusVerifyFailed = 4;
constexpr int StatusRefused = 5;
constexpr int StatusImageRejected = 6;
// Erasing slot B takes up to about 2 s per 4 KB in the worst case.
constexpr int EraseTimeoutMs = 120'000;
constexpr int WriteTimeoutMs = 3000;

QByteArray request(char op, const QByteArray &args = {})
{
    QByteArray r;
    r.append(Start);
    r.append(op);
    r.append(args);
    return r;
}

QByteArray le32(quint32 v)
{
    QByteArray b(4, '\0');
    qToLittleEndian(v, b.data());
    return b;
}

QByteArray le16(quint16 v)
{
    QByteArray b(2, '\0');
    qToLittleEndian(v, b.data());
    return b;
}

QString statusText(int status)
{
    switch (status) {
    case 0:
        return QStringLiteral("ok");
    case StatusBadCrc:
        return QStringLiteral("data checksum wrong");
    case 2:
        return QStringLiteral("out of range");
    case StatusFlashError:
        return QStringLiteral("flash error (not erased?)");
    case StatusVerifyFailed:
        return QStringLiteral("read back differs");
    case StatusRefused:
        return QStringLiteral("refused in this state");
    case StatusImageRejected:
        return QStringLiteral("image rejected");
    default:
        return QStringLiteral("status %1").arg(status);
    }
}

QString problemText(int problem)
{
    switch (problem) {
    case 1:
        return QStringLiteral("bad length");
    case 2:
        return QStringLiteral("initial stack pointer not in RAM");
    case 3:
        return QStringLiteral("reset handler not inside the image");
    case 4:
        return QStringLiteral("no image info block");
    case 5:
        return QStringLiteral("linked for another address");
    case 6:
        return QStringLiteral("length differs from the info block");
    case 7:
        return QStringLiteral("CRC-32 differs");
    case 8:
        return QStringLiteral("flash read error");
    default:
        return QStringLiteral("problem %1").arg(problem);
    }
}

// Reads a length-prefixed text at @p at and moves past it.
bool takeText(const QByteArray &p, int &at, QString &text)
{
    if (at >= p.size()) {
        return false;
    }
    const int n = static_cast<quint8>(p.at(at));
    if (at + 1 + n > p.size()) {
        return false;
    }
    text = QString::fromLatin1(p.mid(at + 1, n));
    at += 1 + n;
    return true;
}

quint32 crcOfErased(int length)
{
    return dmmCrc32(QByteArray(length, '\xFF'));
}

} // namespace

QString slotStateName(int state)
{
    switch (state) {
    case 0:
        return QStringLiteral("empty");
    case 1:
        return QStringLiteral("confirmed");
    case 2:
        return QStringLiteral("on trial");
    case 3:
        return QStringLiteral("failed");
    case 4:
        return QStringLiteral("downloaded, waiting to go in");
    default:
        return QStringLiteral("state %1").arg(state);
    }
}

QString fallbackName(int fallback)
{
    switch (fallback) {
    case 0:
        return QStringLiteral("none");
    case 1:
        return QStringLiteral("the new image used its starts without being confirmed");
    case 2:
        return QStringLiteral("the new image failed its check");
    case 3:
        return QStringLiteral("asked for");
    case 4:
        return QStringLiteral("no image in the run slot");
    default:
        return QStringLiteral("reason %1").arg(fallback);
    }
}

QString stayReasonName(int reason)
{
    switch (reason) {
    case 1:
        return QStringLiteral("asked for");
    case 2:
        return QStringLiteral("force key held at power-on");
    case 3:
        return QStringLiteral("no image to start");
    case 4:
        return QStringLiteral("flash error");
    default:
        return QStringLiteral("-");
    }
}

DmmPortKind probeDmmPort(Transport &transport, int timeoutMs)
{
    QByteArray payload;
    quint8 status = 0;
    QString error;
    if (!dmmExchange(transport, request('i'), payload, status, &error, timeoutMs) || status != 0) {
        return DmmPortKind::None;
    }
    return payload.startsWith("DMLD") ? DmmPortKind::Loader : DmmPortKind::App;
}

LoaderClient::LoaderClient(Transport &transport)
    : m_transport(transport)
{
}

std::optional<LoaderInfo> LoaderClient::parseInfo(const QByteArray &p)
{
    if (!p.startsWith("DMLD") || p.size() < 6) {
        return std::nullopt;
    }
    LoaderInfo i;
    i.protocol = static_cast<quint8>(p.at(4));
    int at = 5;
    if (!takeText(p, at, i.model) || !takeText(p, at, i.version) || at + 16 > p.size()) {
        return std::nullopt;
    }
    i.slotA = qFromLittleEndian<quint32>(p.constData() + at);
    i.slotASize = qFromLittleEndian<quint32>(p.constData() + at + 4);
    i.slotB = qFromLittleEndian<quint32>(p.constData() + at + 8);
    i.slotBSize = qFromLittleEndian<quint32>(p.constData() + at + 12);
    at += 16;
    for (LoaderSlot *slot : {&i.a, &i.b}) {
        if (at + 10 > p.size()) {
            return std::nullopt;
        }
        slot->state = static_cast<quint8>(p.at(at));
        slot->manual = p.at(at + 1) != 0;
        slot->length = qFromLittleEndian<quint32>(p.constData() + at + 2);
        slot->crc = qFromLittleEndian<quint32>(p.constData() + at + 6);
        at += 10;
        if (!takeText(p, at, slot->version)) {
            return std::nullopt;
        }
    }
    if (at + 5 > p.size()) {
        return std::nullopt;
    }
    i.attempts = static_cast<quint8>(p.at(at));
    i.maxAttempts = static_cast<quint8>(p.at(at + 1));
    i.fallback = static_cast<quint8>(p.at(at + 2));
    i.swapActive = p.at(at + 3) != 0;
    i.stayReason = static_cast<quint8>(p.at(at + 4));
    return i;
}

bool LoaderClient::simple(const QByteArray &r, QString *error, int timeoutMs, int *status, QByteArray *payload)
{
    QByteArray body;
    quint8 s = 0;
    if (status) {
        *status = -1;
    }
    if (!dmmExchange(m_transport, r, body, s, error, timeoutMs)) {
        return false;
    }
    if (status) {
        *status = s;
    }
    if (payload) {
        *payload = body;
    }
    if (s != 0) {
        *error = statusText(s);
        if (s == StatusImageRejected && !body.isEmpty()) {
            *error += QStringLiteral(": ") + problemText(static_cast<quint8>(body.at(0)));
        }
        return false;
    }
    return true;
}

std::optional<LoaderInfo> LoaderClient::info(QString *error, int timeoutMs)
{
    QByteArray payload;
    if (!simple(request('i'), error, timeoutMs, nullptr, &payload)) {
        return std::nullopt;
    }
    auto i = parseInfo(payload);
    if (!i) {
        *error = QStringLiteral("not a DeltaMM loader information reply");
    }
    return i;
}

bool LoaderClient::erase(quint32 length, QString *error)
{
    return simple(request('e', le32(length)), error, EraseTimeoutMs);
}

bool LoaderClient::write(quint32 offset, const QByteArray &data, QString *error, int *status, bool badCrc)
{
    const quint32 crc = dmmCrc32(data) ^ (badCrc ? 1u : 0u);
    return simple(request('w', le32(offset) + le16(static_cast<quint16>(data.size())) + data + le32(crc)), error, WriteTimeoutMs,
        status);
}

std::optional<quint32> LoaderClient::checksum(quint32 offset, quint32 length, QString *error)
{
    QByteArray payload;
    if (!simple(request('k', le32(offset) + le32(length)), error, 5000, nullptr, &payload)) {
        return std::nullopt;
    }
    if (payload.size() != 4) {
        *error = QStringLiteral("unexpected checksum reply");
        return std::nullopt;
    }
    return qFromLittleEndian<quint32>(payload.constData());
}

bool LoaderClient::commit(quint32 length, quint32 crc, bool manual, QString *error, int *problem)
{
    int status = 0;
    QByteArray payload;
    const bool ok = simple(request('c', le32(length) + le32(crc) + QByteArray(1, manual ? '\x01' : '\x00')), error, 20'000,
        &status, &payload);
    if (problem) {
        *problem = status == StatusImageRejected && !payload.isEmpty() ? static_cast<quint8>(payload.at(0)) : 0;
    }
    return ok;
}

bool LoaderClient::restart(int mode, quint16 argument, QString *error)
{
    return simple(request('b', QByteArray(1, static_cast<char>(mode)) + le16(argument)), error, 2000);
}

bool LoaderClient::upload(const QByteArray &image, bool manual, UploadReport &report, QString *error, const Progress &progress,
    int corruptChunk)
{
    const auto before = info(error);
    if (!before) {
        return false;
    }
    if (static_cast<quint32>(image.size()) > before->slotBSize) {
        *error = QStringLiteral("the image is %1 bytes, more than the spare slot's %2").arg(image.size()).arg(before->slotBSize);
        return false;
    }
    if (before->b.state == 1 && before->a.state != 1) {
        *error = QStringLiteral("the spare slot holds the only confirmed image: confirm the running image or roll back first");
        return false;
    }
    const int chunks = static_cast<int>((image.size() + ChunkBytes - 1) / ChunkBytes);
    QList<bool> done(chunks, false);

    // An erased slot B may already hold part of this image from a broken
    // upload: keep the chunks that match, write the erased ones.
    bool needErase = before->b.state != 0;
    for (int c = 0; c < chunks && !needErase; ++c) {
        const QByteArray chunk = image.mid(c * ChunkBytes, ChunkBytes);
        const auto crc = checksum(static_cast<quint32>(c * ChunkBytes), static_cast<quint32>(chunk.size()), error);
        if (!crc) {
            return false;
        }
        if (*crc == dmmCrc32(chunk)) {
            done[c] = true;
            ++report.skippedChunks;
        } else if (*crc != crcOfErased(static_cast<int>(chunk.size()))) {
            needErase = true;
        }
    }

    for (int round = 0; round < 2; ++round) {
        if (needErase) {
            if (!erase(static_cast<quint32>(image.size()), error)) {
                *error = QStringLiteral("erasing the spare slot: ") + *error;
                return false;
            }
            report.erased = true;
            report.skippedChunks = 0;
            done.fill(false);
            needErase = false;
        }
        bool restartRound = false;
        for (int c = 0; c < chunks && !restartRound; ++c) {
            if (done[c]) {
                continue;
            }
            const quint32 offset = static_cast<quint32>(c * ChunkBytes);
            QByteArray chunk = image.mid(c * ChunkBytes, ChunkBytes);
            // Lengths are whole words; the image ends on one.
            while (chunk.size() % 4 != 0) {
                chunk.append('\xFF');
            }
            for (int attempt = 0; attempt < 4 && !done[c]; ++attempt) {
                int status = -1;
                const bool corrupt = c == corruptChunk && attempt == 0;
                if (write(offset, chunk, error, &status, corrupt)) {
                    done[c] = true;
                    break;
                }
                ++report.retries;
                if (status == StatusBadCrc || status == -1 || status == StatusVerifyFailed || status == StatusFlashError) {
                    // Whatever got programmed: the chunk, or nothing, or a
                    // part, which needs the slot erased again.
                    m_transport.discardInput();
                    const auto crc = checksum(offset, static_cast<quint32>(chunk.size()), error);
                    if (crc && *crc == dmmCrc32(chunk)) {
                        done[c] = true;
                    } else if (crc && *crc != crcOfErased(static_cast<int>(chunk.size()))) {
                        restartRound = true;
                        break;
                    }
                } else {
                    *error = QStringLiteral("writing at 0x%1: %2").arg(offset, 0, 16).arg(*error);
                    return false;
                }
            }
            if (!done[c] && !restartRound) {
                *error = QStringLiteral("writing at 0x%1 failed: %2").arg(offset, 0, 16).arg(*error);
                return false;
            }
            ++report.writtenChunks;
            if (progress) {
                progress(qMin<qint64>(offset + ChunkBytes, image.size()), image.size());
            }
        }
        if (!restartRound) {
            int problem = 0;
            if (!commit(static_cast<quint32>(image.size()), dmmCrc32(image), manual, error, &problem)) {
                *error = QStringLiteral("commit: ") + *error;
                return false;
            }
            return true;
        }
        needErase = true;
    }
    *error = QStringLiteral("the spare slot could not be written");
    return false;
}
