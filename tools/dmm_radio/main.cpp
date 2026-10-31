// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

// dmm-radio: PC tool for radios running factory firmware or DeltaMM.
// This version backs up a radio's per-unit data over the programming link.

#include "backup.h"
#include "cps_client.h"
#include "deltamm_client.h"
#include "serial_transport.h"
#include "stock_layout.h"
#include "update_client.h"
#include "update_files.h"

#include "dmm/version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>
#include <QThread>

#include <utility>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("deltamm"), QStringLiteral("dmm-radio"));
}

QString pickPort(const QString &requested)
{
    if (!requested.isEmpty()) {
        return requested;
    }
    const auto radios = SerialTransport::findRadios();
    if (radios.size() == 1) {
        return radios.constFirst().portName();
    }
    if (radios.isEmpty()) {
        err() << "No radio found. Connect it with the programming cable and switch it on, or name the port with --port." << Qt::endl;
    } else {
        err() << "Several radios found; choose one with --port:" << Qt::endl;
        for (const auto &r : radios) {
            err() << "  " << r.portName() << Qt::endl;
        }
    }
    return {};
}

void printBackupSummary(const Backup &b)
{
    out() << "Radio:    " << b.identity.model << " (radio version " << b.identity.version << ")" << Qt::endl;
    out() << "Radio id: " << b.radioId() << Qt::endl;
    for (auto it = b.data.cbegin(); it != b.data.cend(); ++it) {
        out() << "  " << it.key() << ": " << it.value().size() << " bytes" << Qt::endl;
    }
    for (const QString &w : b.warnings) {
        out() << "Warning:  " << w << Qt::endl;
    }
    for (const QString &p : b.problems) {
        out() << "Problem:  " << p << Qt::endl;
    }
    out() << (b.verified() ? "Verified: yes" : "Verified: NO") << (b.complete ? "" : " (quick backup, system area not included)") << Qt::endl;
}

int commandPorts()
{
    const auto radios = SerialTransport::findRadios();
    const auto deltaMm = SerialTransport::findDeltaMmPorts();
    if (radios.isEmpty() && deltaMm.isEmpty()) {
        out() << "No radio found." << Qt::endl;
    }
    for (const auto &r : radios) {
        out() << r.portName() << "  " << r.description() << "  (factory firmware or update mode)" << Qt::endl;
    }
    for (const auto &r : deltaMm) {
        out() << r.portName() << "  " << r.description() << "  (DeltaMM)" << Qt::endl;
    }
    return 0;
}

int commandBackup(const QString &portRequest, bool quick, const QString &root)
{
    const QString port = pickPort(portRequest);
    if (port.isEmpty()) {
        return 2;
    }
    SerialTransport transport;
    QString error;
    if (!transport.open(port, &error)) {
        err() << "Cannot open " << port << ": " << error << Qt::endl;
        return 2;
    }
    CpsClient client(transport);
    if (!client.enterProgramMode(&error)) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    const auto identity = client.identify(&error);
    const RadioLayout *layout = identity ? layoutForModel(identity->model) : nullptr;
    if (!layout) {
        err() << "Error: " << (identity ? QStringLiteral("unsupported radio model %1").arg(identity->model) : error) << Qt::endl;
        client.leaveProgramMode(nullptr);
        return 2;
    }
    out() << "Backing up " << identity->model << " on " << port << ". The radio shows PC READ and restarts when done." << Qt::endl;

    BackupOptions options;
    options.quick = quick;
    int lastPercent = -1;
    QString current;
    options.partStarted = [&](const QString &part) {
        current = part;
        lastPercent = -1;
    };
    options.progress = [&](qint64 done, qint64 total) {
        const int percent = static_cast<int>(done * 100 / total);
        if (percent != lastPercent) {
            lastPercent = percent;
            out() << "\r  " << current << ": " << percent << " %" << Qt::flush;
            if (percent == 100) {
                out() << Qt::endl;
            }
        }
    };
    Backup backup;
    const bool ok = readBackup(client, *identity, *layout, options, backup, &error);
    QString endError;
    if (!client.leaveProgramMode(&endError)) {
        err() << "Note: " << endError << ". Switch the radio off and on if it stays on the PC screen." << Qt::endl;
    }
    if (!ok) {
        err() << Qt::endl
              << "Error: " << error << ". Nothing was saved." << Qt::endl;
        return 2;
    }
    const QString folder = saveBackup(backup, root, &error);
    if (folder.isEmpty()) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    printBackupSummary(backup);
    out() << "Saved to: " << QDir::toNativeSeparators(folder) << Qt::endl;
    return backup.verified() ? 0 : 1;
}

int commandVerify(const QString &folder)
{
    Backup backup;
    QString error;
    if (!loadBackup(folder, backup, &error)) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    printBackupSummary(backup);
    return backup.verified() ? 0 : 1;
}

int commandList(const QString &root)
{
    QDir dir(root);
    const auto radios = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (radios.isEmpty()) {
        out() << "No backups in " << QDir::toNativeSeparators(root) << Qt::endl;
    }
    for (const QString &radio : radios) {
        const auto times = QDir(dir.filePath(radio)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &time : times) {
            Backup b;
            const bool loaded = loadBackup(dir.filePath(radio + QLatin1Char('/') + time), b, nullptr);
            out() << radio << '/' << time << "  " << (loaded ? b.identity.model : QStringLiteral("?")) << "  "
                  << (loaded && b.verified() ? "verified" : "NOT verified") << (b.complete ? "" : ", quick") << Qt::endl;
        }
    }
    return 0;
}

int commandConfig(const QStringList &args)
{
    QSettings s = settings();
    if (args.size() == 2 && args.at(0) == QStringLiteral("backup-before-flash") && (args.at(1) == QStringLiteral("on") || args.at(1) == QStringLiteral("off"))) {
        s.setValue(QStringLiteral("backupBeforeFlash"), args.at(1) == QStringLiteral("on"));
    } else if (!args.isEmpty()) {
        err() << "Usage: dmm-radio config [backup-before-flash on|off]" << Qt::endl;
        return 2;
    }
    out() << "backup-before-flash " << (s.value(QStringLiteral("backupBeforeFlash"), true).toBool() ? "on" : "off") << Qt::endl;
    out() << "settings file: " << QDir::toNativeSeparators(s.fileName()) << Qt::endl;
    return 0;
}

bool readFile(const QString &path, QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err() << "Error: cannot read " << QDir::toNativeSeparators(path) << Qt::endl;
        return false;
    }
    data = f.readAll();
    return true;
}

// The .spi or .CDI file next to a .CDD file, in either letter case.
QString sibling(const QString &cdd, const QString &suffix)
{
    const QFileInfo info(cdd);
    for (const QString &s : {suffix, suffix.toUpper(), suffix.toLower()}) {
        const QString path = info.dir().filePath(info.completeBaseName() + s);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return info.dir().filePath(info.completeBaseName() + suffix);
}

// True when a verified backup exists or the check is turned off.
bool backupCheckPasses(const QString &radioId, const QString &root)
{
    if (!settings().value(QStringLiteral("backupBeforeFlash"), true).toBool()) {
        out() << "Backup check turned off (dmm-radio config backup-before-flash off)." << Qt::endl;
        return true;
    }
    QString error;
    const QString folder = findVerifiedBackup(root, radioId, &error);
    if (folder.isEmpty()) {
        err() << "Refused: " << error << "." << Qt::endl
              << "Run 'dmm-radio backup' with the radio still on factory firmware first." << Qt::endl;
        return false;
    }
    out() << "Backup:   " << QDir::toNativeSeparators(folder) << " (verified)" << Qt::endl;
    return true;
}

// Reads a .CDD file and the .spi and .CDI files next to it.
bool readUpdateFiles(const QString &cddPath, UpdateFiles &files)
{
    return readFile(cddPath, files.image) && readFile(sibling(cddPath, QStringLiteral(".spi")), files.spi) &&
        readFile(sibling(cddPath, QStringLiteral(".CDI")), files.cdi);
}

int commandPrepare(const QStringList &args, const QString &radioId, const QString &root, const QString &name)
{
    if (args.size() != 3) {
        err() << "Usage: dmm-radio prepare <image.bin> <factory .CDD> <output folder>" << Qt::endl;
        return 2;
    }
    const QString &imagePath = args.at(0);
    const QString &stockPath = args.at(1);
    const QString &outDir = args.at(2);

    if (!backupCheckPasses(radioId, root)) {
        return 1;
    }

    UpdateFiles stock;
    QByteArray image;
    if (!readUpdateFiles(stockPath, stock) || !readFile(imagePath, image)) {
        return 2;
    }
    QString problem = checkStockFiles(stock);
    if (!problem.isEmpty()) {
        err() << "Error: factory firmware files: " << problem << "." << Qt::endl;
        return 2;
    }
    problem = checkImage(image);
    if (!problem.isEmpty()) {
        err() << "Error: " << problem << "." << Qt::endl;
        return 2;
    }

    const UpdateFiles files = makeUpdateFiles(stock, image);
    QDir().mkpath(outDir);
    const QString base = QDir(outDir).filePath(
        name.isEmpty() ? QStringLiteral("DeltaMM_%1").arg(QString::fromLatin1(dmm::Version)) : name);
    for (const auto &[suffix, data] : {std::pair{QStringLiteral(".CDD"), &files.image}, std::pair{QStringLiteral(".spi"), &files.spi},
             std::pair{QStringLiteral(".CDI"), &files.cdi}}) {
        QFile f(base + suffix);
        if (!f.open(QIODevice::WriteOnly) || f.write(*data) != data->size()) {
            err() << "Error: cannot write " << QDir::toNativeSeparators(base + suffix) << Qt::endl;
            return 2;
        }
    }
    out() << "Image:    " << image.size() << " bytes, SHA-256 "
          << QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex() << Qt::endl;
    out() << "Written:  " << QDir::toNativeSeparators(base) << ".CDD, .spi, .CDI" << Qt::endl;
    return 0;
}

// Opens the DeltaMM data port: @p requested, or the one port of a radio
// running DeltaMM that answers the data protocol.
bool openDeltaMm(const QString &requested, SerialTransport &transport, QString &portName)
{
    QString error;
    if (!requested.isEmpty()) {
        portName = requested;
        if (!transport.open(requested, &error)) {
            err() << "Cannot open " << requested << ": " << error << Qt::endl;
            return false;
        }
        return true;
    }
    const auto ports = SerialTransport::findDeltaMmPorts();
    for (const QSerialPortInfo &info : ports) {
        if (!transport.open(info.portName(), &error)) {
            continue;
        }
        DeltaMmClient client(transport);
        if (client.info(&error, 400)) {
            portName = info.portName();
            return true;
        }
        // The log port's console: end the line the probe left behind.
        transport.write(QByteArray("\n"));
    }
    err() << (ports.isEmpty() ? "No radio running DeltaMM found." : "No DeltaMM data port answered.")
          << " Name the port with --port." << Qt::endl;
    return false;
}

void printProgress(qint64 done, qint64 total)
{
    static int last = -1;
    const int percent = static_cast<int>(done * 100 / total);
    if (percent != last) {
        last = percent;
        out() << "\r  " << percent << " %" << Qt::flush;
        if (percent == 100) {
            out() << Qt::endl;
            last = -1;
        }
    }
}

bool parseNumber(const QString &text, quint32 &value)
{
    bool ok = false;
    value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? text.mid(2).toUInt(&ok, 16) : text.toUInt(&ok, 10);
    return ok;
}

void printReadReport(const DeltaMmClient::ReadReport &report)
{
    if (report.correctedChunks > 0) {
        out() << "Note:     " << report.correctedChunks << " chunks had bit errors the flash corrected" << Qt::endl;
    }
    for (quint32 at : report.flaggedChunks) {
        out() << "Note:     the flash flags the 4 KB at 0x" << QString::number(at, 16)
              << " (its check fails on pieces written separately; data as read)" << Qt::endl;
    }
    for (quint32 at : report.badChunks) {
        out() << "Problem:  the 4 KB at 0x" << QString::number(at, 16) << " could not be read" << Qt::endl;
    }
}

int commandInfo(const QString &portRequest)
{
    SerialTransport transport;
    QString port;
    if (!openDeltaMm(portRequest, transport, port)) {
        return 2;
    }
    DeltaMmClient client(transport);
    QString error;
    const auto info = client.info(&error);
    if (!info) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    out() << "Port:     " << port << Qt::endl;
    out() << "DeltaMM:  " << info->version << Qt::endl;
    if (!info->storagePresent) {
        out() << "Storage:  none found (id " << QString::number(info->maker, 16) << " " << QString::number(info->device, 16) << ")" << Qt::endl;
        return 0;
    }
    out() << "Storage:  id " << QString::number(info->maker, 16) << " " << QString::number(info->device, 16) << ", "
          << info->storageBytes / (1024 * 1024) << " MB, "
          << (info->remappedBlocks < 0 ? QStringLiteral("no bad-block table") : QStringLiteral("%1 blocks remapped").arg(info->remappedBlocks))
          << Qt::endl;
    return 0;
}

int commandDump(const QStringList &args, const QString &portRequest, bool physical)
{
    quint32 address = 0;
    quint32 length = 0;
    if (args.size() != 3 || !parseNumber(args.at(0), address) || !parseNumber(args.at(1), length) || length == 0) {
        err() << "Usage: dmm-radio dump <address> <length> <file> [--physical]" << Qt::endl;
        return 2;
    }
    SerialTransport transport;
    QString port;
    if (!openDeltaMm(portRequest, transport, port)) {
        return 2;
    }
    DeltaMmClient client(transport);
    QByteArray data;
    DeltaMmClient::ReadReport report;
    QString error;
    out() << "Reading " << length << " bytes from 0x" << QString::number(address, 16) << (physical ? " (physical)" : "") << Qt::endl;
    if (!client.read(address, length, physical, data, report, &error, printProgress)) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    QFile file(args.at(2));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        err() << "Error: cannot write " << args.at(2) << Qt::endl;
        return 2;
    }
    out() << "Written:  " << QDir::toNativeSeparators(args.at(2)) << ", SHA-256 "
          << QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() << Qt::endl;
    printReadReport(report);
    return report.badChunks.isEmpty() ? 0 : 1;
}

// Reads the system area as DeltaMM sees it and compares it with a backup
// made over the factory firmware's programming link.
int commandCheckBackup(const QStringList &args, const QString &portRequest, const QString &radioId, const QString &root)
{
    QString error;
    QString folder = args.value(0);
    if (folder.isEmpty()) {
        folder = findVerifiedBackup(root, radioId, &error);
        if (folder.isEmpty()) {
            err() << "Error: " << error << Qt::endl;
            return 2;
        }
    }
    Backup backup;
    if (!loadBackup(folder, backup, &error)) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    const RadioLayout *layout = layoutForModel(backup.identity.model);
    if (layout == nullptr) {
        err() << "Error: the backup is of an unsupported model" << Qt::endl;
        return 2;
    }
    out() << "Backup:   " << QDir::toNativeSeparators(folder) << (backup.verified() ? "" : " (NOT verified)") << Qt::endl;

    SerialTransport transport;
    QString port;
    if (!openDeltaMm(portRequest, transport, port)) {
        return 2;
    }
    // The system area, plus the block after it: its last block's other copy.
    constexpr quint32 Base = 0x02F00000;
    constexpr quint32 Length = 0x100000;
    DeltaMmClient client(transport);
    QByteArray image;
    DeltaMmClient::ReadReport report;
    out() << "Reading the system area from the radio (1 MB)" << Qt::endl;
    if (!client.read(Base, Length, false, image, report, &error, printProgress)) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    printReadReport(report);

    // Flagged chunks count by their bytes: the comparison below decides.
    bool allMatch = report.badChunks.isEmpty();
    for (const BackupPart &part : layout->parts) {
        if (!backup.data.contains(part.name)) {
            continue;
        }
        const QByteArray saved = backup.data.value(part.name);
        const QByteArray now = stock_layout::linkView(image, Base, part.address, part.length);
        if (now.isEmpty()) {
            out() << "  " << part.name << ": not in the area read" << Qt::endl;
            allMatch = false;
            continue;
        }
        qsizetype first = -1;
        qsizetype differing = 0;
        for (qsizetype i = 0; i < qMin(saved.size(), now.size()); ++i) {
            if (saved.at(i) != now.at(i)) {
                if (first < 0) {
                    first = i;
                }
                ++differing;
            }
        }
        if (saved.size() == now.size() && differing == 0) {
            out() << "  " << part.name << ": identical (" << saved.size() << " bytes)" << Qt::endl;
        } else {
            allMatch = false;
            out() << "  " << part.name << ": DIFFERENT, " << differing << " bytes, first at 0x"
                  << QString::number(part.address + static_cast<quint32>(first), 16) << Qt::endl;
        }
    }
    out() << (allMatch ? "Result:   the radio's data matches the backup" : "Result:   MISMATCH") << Qt::endl;
    return allMatch ? 0 : 1;
}

} // namespace

constexpr const char *UpdateModeHint = "Switch the radio off, then on while holding PTT and PF3, the top key (red LED blinking).";

// Waits a few seconds for a radio running DeltaMM and prints its version.
void reportStartedFirmware()
{
    QDeadlineTimer deadline{std::chrono::seconds(15)};
    while (!deadline.hasExpired()) {
        for (const QSerialPortInfo &info : SerialTransport::findDeltaMmPorts()) {
            SerialTransport transport;
            QString error;
            if (!transport.open(info.portName(), &error)) {
                continue;
            }
            DeltaMmClient client(transport);
            if (const auto running = client.info(&error, 400)) {
                out() << "Running:  DeltaMM " << running->version << " on " << info.portName() << Qt::endl;
                return;
            }
            transport.write(QByteArray("\n"));
        }
        QThread::msleep(500);
    }
    out() << "Running:  no DeltaMM port within 15 s (factory firmware, or the radio did not start)." << Qt::endl;
}

// Opens @p port and starts an update session, retrying for a few seconds
// while a radio that just appeared settles.
bool startUpdate(const QString &port, SerialTransport &transport, UpdateClient &client, bool retry, QString *error)
{
    QDeadlineTimer deadline{std::chrono::seconds(retry ? 5 : 0)};
    while (!transport.open(port, error)) {
        if (deadline.hasExpired()) {
            return false;
        }
        QThread::msleep(500);
    }
    while (!client.start(error)) {
        if (deadline.hasExpired()) {
            return false;
        }
        QThread::msleep(500);
    }
    return true;
}

// Waits up to @p seconds for a radio in update mode and starts the update on
// it; returns its port, or an empty string. A radio running its factory
// firmware has the same USB IDs as one in update mode, so a radio counts
// only once it answers the start of an update. One that does not is taken
// as switched on normally, and the wait goes on until it has been off.
QString waitAndStartUpdate(int seconds, SerialTransport &transport, UpdateClient &client)
{
    out() << "Waiting up to " << seconds << " s for the radio in update mode. " << UpdateModeHint << Qt::endl;
    bool ask = true;
    bool told = false;
    QDeadlineTimer deadline{std::chrono::seconds(seconds)};
    while (!deadline.hasExpired()) {
        const auto radios = SerialTransport::findRadios();
        if (radios.isEmpty()) {
            ask = true;
        } else if (ask && radios.size() > 1) {
            err() << "Several radios found; choose one with --port." << Qt::endl;
            return {};
        } else if (ask) {
            ask = false;
            const QString port = radios.constFirst().portName();
            QString error;
            if (startUpdate(port, transport, client, true, &error)) {
                return port;
            }
            transport.close();
            if (!told) {
                out() << "No answer on " << port << ": the radio is not in update mode. " << UpdateModeHint
                      << Qt::endl;
                told = true;
            }
        }
        QThread::msleep(300);
    }
    err() << "No radio in update mode appeared." << Qt::endl;
    return {};
}

// Sends a firmware set to a radio in update mode, optionally waiting for
// the radio to be switched on in it.
int commandFlash(const QStringList &args, const QString &portRequest, const QString &radioId, const QString &root, int waitSeconds)
{
    if (args.size() != 1) {
        err() << "Usage: dmm-radio flash <.CDD file> (with its .spi and .CDI next to it)" << Qt::endl;
        return 2;
    }
    if (!backupCheckPasses(radioId, root)) {
        return 1;
    }
    UpdateFiles files;
    if (!readUpdateFiles(args.at(0), files)) {
        return 2;
    }
    QString problem = checkStockFiles(files);
    if (problem.isEmpty()) {
        problem = checkImage(files.image);
    }
    if (!problem.isEmpty()) {
        err() << "Error: " << problem << "." << Qt::endl;
        return 2;
    }

    const bool wait = waitSeconds > 0 && portRequest.isEmpty();
    if (!wait && portRequest.isEmpty() && SerialTransport::findRadios().isEmpty() && !SerialTransport::findDeltaMmPorts().isEmpty()) {
        err() << "The radio is running DeltaMM. " << UpdateModeHint << Qt::endl;
        return 2;
    }
    SerialTransport transport;
    UpdateClient client(transport);
    QString error;
    const QString port = wait ? waitAndStartUpdate(waitSeconds, transport, client) : pickPort(portRequest);
    if (port.isEmpty()) {
        return 2;
    }
    if (!wait && !startUpdate(port, transport, client, false, &error)) {
        err() << "Error on " << port << ": " << error << "." << Qt::endl
              << "Is the radio in update mode? " << UpdateModeHint << Qt::endl;
        return 2;
    }
    const auto identity = client.identify(&error);
    if (!identity) {
        err() << "Error: " << error << Qt::endl;
        return 2;
    }
    if (layoutForModel(identity->model) == nullptr) {
        err() << "Refused: the radio reports model " << identity->model << ", which dmm-radio does not know." << Qt::endl;
        return 2;
    }
    out() << "Radio:    " << identity->model << " (boot loader " << identity->version << ") on " << port << Qt::endl;
    out() << "Image:    " << files.image.size() << " bytes, SHA-256 "
          << QCryptographicHash::hash(files.image, QCryptographicHash::Sha256).toHex() << Qt::endl;

    if (!client.write(ImageLoadAddress, files.image, &error, printProgress) || !client.finish(&error)) {
        err() << Qt::endl
              << "Error: " << error << "." << Qt::endl
              << "The radio stays in update mode: run flash again, with these files or the factory ones." << Qt::endl;
        return 2;
    }
    out() << "Done. The radio starts the new firmware." << Qt::endl;
    reportStartedFirmware();
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dmm-radio"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(dmm::Version));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "DeltaMM radio tool.\n\n"
        "Commands:\n"
        "  ports                 list connected radios\n"
        "  backup                save calibration, radio info and the system area\n"
        "  verify <folder>       re-check a saved backup\n"
        "  list                  list saved backups\n"
        "  prepare <image.bin> <factory .CDD> <folder>\n"
        "                        make update files; needs a verified backup\n"
        "On a radio in update mode (red LED blinking):\n"
        "  flash <.CDD file> [--wait <s>]\n"
        "                        send a firmware set (.CDD, .spi, .CDI); --wait\n"
        "                        waits for the radio to enter update mode\n"
        "On a radio running DeltaMM:\n"
        "  info                  version and storage\n"
        "  dump <addr> <len> <file> [--physical]\n"
        "                        save storage bytes (remapped)\n"
        "  check-backup [folder] compare the radio with a backup\n"
        "  config [backup-before-flash on|off]"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("Serial port of the radio."), QStringLiteral("name"));
    const QCommandLineOption quickOption(QStringLiteral("quick"), QStringLiteral("Back up calibration and radio info only (seconds instead of minutes)."));
    const QCommandLineOption dirOption(QStringLiteral("dir"), QStringLiteral("Backup folder (default: the user data folder)."), QStringLiteral("path"));
    const QCommandLineOption radioOption(QStringLiteral("radio"), QStringLiteral("Radio id (backup folder name) for prepare and flash."), QStringLiteral("id"));
    const QCommandLineOption physicalOption(QStringLiteral("physical"), QStringLiteral("dump: skip the bad-block remapping."));
    const QCommandLineOption nameOption(QStringLiteral("name"),
        QStringLiteral("prepare: file name without extension (default: DeltaMM_ and this tool's version)."), QStringLiteral("base"));
    const QCommandLineOption waitOption(QStringLiteral("wait"),
        QStringLiteral("flash: wait up to this many seconds for the radio to be switched on in update mode."), QStringLiteral("seconds"));
    parser.addOptions({portOption, quickOption, dirOption, radioOption, physicalOption, nameOption, waitOption});
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("ports, backup, verify, list, prepare, flash, info, dump, check-backup or config"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(2);
    }
    const QString command = args.constFirst();
    const QString root = parser.isSet(dirOption) ? parser.value(dirOption) : defaultBackupRoot();
    if (command == QStringLiteral("ports")) {
        return commandPorts();
    }
    if (command == QStringLiteral("backup")) {
        return commandBackup(parser.value(portOption), parser.isSet(quickOption), root);
    }
    if (command == QStringLiteral("verify") && args.size() == 2) {
        return commandVerify(args.at(1));
    }
    if (command == QStringLiteral("list")) {
        return commandList(root);
    }
    if (command == QStringLiteral("prepare")) {
        return commandPrepare(args.mid(1), parser.value(radioOption), root, parser.value(nameOption));
    }
    if (command == QStringLiteral("flash")) {
        return commandFlash(args.mid(1), parser.value(portOption), parser.value(radioOption), root, parser.value(waitOption).toInt());
    }
    if (command == QStringLiteral("info")) {
        return commandInfo(parser.value(portOption));
    }
    if (command == QStringLiteral("dump")) {
        return commandDump(args.mid(1), parser.value(portOption), parser.isSet(physicalOption));
    }
    if (command == QStringLiteral("check-backup")) {
        return commandCheckBackup(args.mid(1), parser.value(portOption), parser.value(radioOption), root);
    }
    if (command == QStringLiteral("config")) {
        return commandConfig(args.mid(1));
    }
    parser.showHelp(2);
}
