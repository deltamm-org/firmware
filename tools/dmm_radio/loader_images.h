// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The images of a radio with DeltaMM's loader (layout in
// services/d878uv2_boot_layout.h): checks, and the first-install image the
// radio's update mode writes from 0x08004000.

#include <QByteArray>
#include <QString>

#include <optional>

/// The info block every DeltaMM image carries after its vector table.
struct ImageInfoBlock
{
    /// 1 app, 2 loader, 3 stub.
    quint32 kind = 0;
    quint32 base = 0;
    quint32 end = 0;
    QString version;
};

std::optional<ImageInfoBlock> readImageInfo(const QByteArray &image);

/// An app image for the loader's slot: linked for slot A, its length as
/// its info block says, at most the spare slot's size. Empty, or why not.
QString checkSlotImage(const QByteArray &image);

/// The loader image as built (dmm_loader.bin). Empty, or why not.
QString checkLoaderImage(const QByteArray &loader);

/// The stub as built (dmm_stub.bin). Empty, or why not.
QString checkStubImage(const QByteArray &stub);

/// The image for the radio's update mode that installs the loader: the
/// stub padded to its 4 KB, the app padded to the end of slot A, erased
/// bytes over slot B, the record and the reserved sector, then the loader
/// with its CRC-32 appended, padded to its end. All inputs must have
/// passed their checks.
QByteArray makeLoaderInstallImage(const QByteArray &stub, const QByteArray &app, const QByteArray &loader);

/// True when @p image starts with the loader's stub: a first-install image.
bool isLoaderInstallImage(const QByteArray &image);
