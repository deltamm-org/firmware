// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "update_files.h"

#include <QtEndian>

namespace {

// .spi: byte 0 = 0x20, byte 4 = section count, bytes 6..9 = image length,
// then the model and hardware version as text.
constexpr int SpiLengthAt = 6;
constexpr int SpiModelAt = 14;
// .CDI: a 256-byte file name, then per section the load address, the byte
// count and the offset in the image.
constexpr int CdiAddressAt = 256;
constexpr int CdiLengthAt = 260;
constexpr int CdiOffsetAt = 264;

quint32 le32(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint32>(b.constData() + at);
}

void putLe32(QByteArray &b, int at, quint32 value)
{
    qToLittleEndian(value, b.data() + at);
}

} // namespace

QString checkStockFiles(const UpdateFiles &stock)
{
    if (stock.spi.size() < SpiModelAt + 7 || stock.cdi.size() < CdiOffsetAt + 4) {
        return QStringLiteral("the .spi or .CDI file is too short");
    }
    if (static_cast<quint8>(stock.spi.at(0)) != 0x20 || stock.spi.at(4) != 1) {
        return QStringLiteral("the .spi header is not a single-section firmware header");
    }
    if (stock.spi.mid(SpiModelAt, 7) != QByteArrayLiteral("D878UV2")) {
        return QStringLiteral("the .spi file is not for the D878UVII");
    }
    const auto length = static_cast<quint32>(stock.image.size());
    if (le32(stock.spi, SpiLengthAt) != length || le32(stock.cdi, CdiLengthAt) != length) {
        return QStringLiteral("the .spi and .CDI lengths do not match the .CDD file");
    }
    if (le32(stock.cdi, CdiAddressAt) != ImageLoadAddress || le32(stock.cdi, CdiOffsetAt) != 0) {
        return QStringLiteral("the .CDI file does not load the image at 0x08004000");
    }
    return {};
}

QString checkImage(const QByteArray &image)
{
    if (image.size() < 8) {
        return QStringLiteral("the image is empty");
    }
    if (static_cast<quint32>(image.size()) > ImageLimitAddress - ImageLoadAddress) {
        return QStringLiteral("the image is %1 bytes, more than fits below the radio data").arg(image.size());
    }
    const quint32 stack = le32(image, 0);
    const quint32 reset = le32(image, 4);
    if (stack < 0x20000000 || stack > 0x20018000 || (stack & 7) != 0) {
        return QStringLiteral("the image's initial stack pointer 0x%1 is not in RAM").arg(stack, 8, 16, QLatin1Char('0'));
    }
    // An image with an info block names the address it is linked for.
    if (image.size() >= 0x150 + 16 && qFromLittleEndian<quint32>(image.constData() + 0x150) == 0x494D4D44 &&
        le32(image, 0x150 + 8) != ImageLoadAddress) {
        return QStringLiteral("the image is linked for 0x%1, not 0x08004000 (an image for the loader's slot: use dmm-radio update)")
            .arg(le32(image, 0x150 + 8), 8, 16, QLatin1Char('0'));
    }
    const quint32 end = ImageLoadAddress + static_cast<quint32>(image.size());
    if ((reset & 1) == 0 || (reset & ~1u) < ImageLoadAddress || (reset & ~1u) >= end) {
        return QStringLiteral("the image's reset handler 0x%1 is not inside the image").arg(reset, 8, 16, QLatin1Char('0'));
    }
    return {};
}

UpdateFiles makeUpdateFiles(const UpdateFiles &stock, const QByteArray &image)
{
    UpdateFiles out{image, stock.spi, stock.cdi};
    const auto length = static_cast<quint32>(image.size());
    putLe32(out.spi, SpiLengthAt, length);
    putLe32(out.cdi, CdiLengthAt, length);
    return out;
}
