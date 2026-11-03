// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "loader_images.h"

#include "zlib_crc.h"

#include "dmm/services/boot_image.h"
#include "dmm/services/d878uv2_boot_layout.h"

#include <QtEndian>

namespace {

namespace layout = dmm::services::d878uv2;
namespace boot = dmm::services::boot;

quint32 le32(const QByteArray &b, qsizetype at)
{
    return qFromLittleEndian<quint32>(b.constData() + at);
}

// Stack pointer in RAM and reset handler inside [base + vector table, end).
QString checkVectors(const QByteArray &image, quint32 base)
{
    const quint32 stack = le32(image, 0);
    const quint32 entry = le32(image, 4);
    if (stack <= layout::RamBase || stack > layout::RamEnd || stack % 4 != 0) {
        return QStringLiteral("the initial stack pointer 0x%1 is not in RAM").arg(stack, 8, 16, QLatin1Char('0'));
    }
    const quint32 target = entry & ~1u;
    if ((entry & 1u) == 0 || target < base + layout::ImageInfoOffset || target >= base + static_cast<quint32>(image.size())) {
        return QStringLiteral("the reset handler 0x%1 is not inside the image").arg(entry, 8, 16, QLatin1Char('0'));
    }
    return {};
}

QString checkKind(const QByteArray &image, quint32 kind, quint32 base, quint32 limit, const QString &what)
{
    const auto info = readImageInfo(image);
    if (!info) {
        return QStringLiteral("no image info block: not a DeltaMM %1 image").arg(what);
    }
    if (info->kind != kind) {
        return QStringLiteral("the image info block names another kind of image than a %1").arg(what);
    }
    if (info->base != base) {
        return QStringLiteral("the %1 is linked for 0x%2, not 0x%3").arg(what).arg(info->base, 8, 16, QLatin1Char('0')).arg(base, 8, 16, QLatin1Char('0'));
    }
    if (info->end - info->base != static_cast<quint32>(image.size())) {
        return QStringLiteral("the file is %1 bytes, its info block says %2").arg(image.size()).arg(info->end - info->base);
    }
    if (static_cast<quint32>(image.size()) > limit) {
        return QStringLiteral("the %1 is %2 bytes, more than its %3").arg(what).arg(image.size()).arg(limit);
    }
    if (image.size() % 4 != 0) {
        return QStringLiteral("the %1's length is not a multiple of 4").arg(what);
    }
    return checkVectors(image, base);
}

} // namespace

std::optional<ImageInfoBlock> readImageInfo(const QByteArray &image)
{
    if (image.size() < static_cast<qsizetype>(layout::ImageInfoOffset + boot::ImageInfoBytes)) {
        return std::nullopt;
    }
    boot::ImageInfo info;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(image.constData()) + layout::ImageInfoOffset;
    if (!boot::parseImageInfo(std::span(bytes, boot::ImageInfoBytes), info)) {
        return std::nullopt;
    }
    return ImageInfoBlock{static_cast<quint32>(info.kind), info.base, info.end, QString::fromLatin1(info.version)};
}

QString checkSlotImage(const QByteArray &image)
{
    if (const auto info = readImageInfo(image); info && info->kind == 1 && info->base == layout::StubBase) {
        return QStringLiteral("this is a plain image for 0x08004000; build with DMM_LOADER=ON (deltamm_loader_slot.bin)");
    }
    return checkKind(image, 1, layout::SlotABase, layout::SlotBEnd - layout::SlotBBase, QStringLiteral("slot image"));
}

QString checkLoaderImage(const QByteArray &loader)
{
    return checkKind(loader, 2, layout::LoaderBase, layout::LoaderEnd - layout::LoaderBase - 4, QStringLiteral("loader"));
}

QString checkStubImage(const QByteArray &stub)
{
    return checkKind(stub, 3, layout::StubBase, layout::StubEnd - layout::StubBase, QStringLiteral("stub"));
}

QByteArray makeLoaderInstallImage(const QByteArray &stub, const QByteArray &app, const QByteArray &loader)
{
    QByteArray image = stub;
    image.append(QByteArray(static_cast<qsizetype>(layout::SlotABase - layout::StubBase) - stub.size(), '\xFF'));
    image.append(app);
    image.append(QByteArray(static_cast<qsizetype>(layout::LoaderBase - layout::SlotABase) - app.size(), '\xFF'));
    image.append(loader);
    QByteArray crc(4, '\0');
    qToLittleEndian(dmmCrc32(loader), crc.data());
    image.append(crc);
    image.append(QByteArray(static_cast<qsizetype>(layout::LoaderEnd - layout::StubBase) - image.size(), '\xFF'));
    return image;
}

bool isLoaderInstallImage(const QByteArray &image)
{
    const auto info = readImageInfo(image);
    return info && info->kind == 3 && info->base == layout::StubBase;
}
