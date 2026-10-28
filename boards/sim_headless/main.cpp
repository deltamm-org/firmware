// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

// Simulator without a window: runs the app on the PC and prints its log
// (and optionally LED changes) to standard output.

#include "dmm/app/app.h"
#include "dmm/os/os.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_codeplug.h"

#include "dmm/board/sim_battery.h"
#include "dmm/board/sim_input.h"
#include "sim_audio.h"
#include "sim_display.h"
#include "sim_storage.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <span>

namespace {

using dmm::services::Leds;

QMutex outputLock;

void printLine(QStringView text)
{
    QMutexLocker guard(&outputLock);
    static QTextStream out(stdout);
    out << text << Qt::endl;
}

class ConsoleLeds final : public Leds
{
public:
    explicit ConsoleLeds(bool print)
        : m_print(print)
    {
    }

    void set(Led led, bool on) override
    {
        const int index = static_cast<int>(led);
        if (m_state[index] == on) {
            return;
        }
        m_state[index] = on;
        if (m_print) {
            const QString name = led == Led::Red ? QStringLiteral("red") : led == Led::Green ? QStringLiteral("green")
                                                                                             : QStringLiteral("blue");
            printLine(QStringLiteral("LED %1 %2").arg(name, on ? QStringLiteral("on") : QStringLiteral("off")));
        }
    }

private:
    bool m_print;
    bool m_state[3] = {false, false, false};
};

// The simulated radio has no power switch or watchdog; quitting the
// program is its power-off.
class SimSystem final : public dmm::services::SystemControl
{
public:
    bool powerSwitchOff() override { return false; }
    void holdPower() override {}
    void releasePower() override {}
    void feedWatchdog() override {}
    void restart() override { dmm::os::requestStop(); }
    dmm::services::ResetReport lastReset() override { return {"simulator start", 0, false, {}}; }
    void testFault() override { dmm::os::requestStop(); }
};

// Prints log lines; console input comes from --command, once.
class ConsoleLog final : public dmm::services::LogSink
{
public:
    void writeLine(std::string_view line) override
    {
        printLine(QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size())));
    }

    std::size_t readInput(std::span<char> buffer) override
    {
        QMutexLocker guard(&m_lock);
        const auto n = std::min(buffer.size(), static_cast<std::size_t>(m_input.size()));
        std::copy_n(m_input.constData(), n, buffer.data());
        m_input.remove(0, static_cast<qsizetype>(n));
        return n;
    }

    void addInput(const QByteArray &input)
    {
        QMutexLocker guard(&m_lock);
        m_input += input;
    }

private:
    QMutex m_lock;
    QByteArray m_input;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sim"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DeltaMM headless simulator"));
    parser.addHelpOption();
    const QCommandLineOption secondsOption(QStringLiteral("seconds"), QStringLiteral("Stop after <n> seconds (0 = run until killed)."), QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption ledsOption(QStringLiteral("leds"), QStringLiteral("Print LED changes."));
    const QCommandLineOption commandOption(QStringLiteral("command"), QStringLiteral("Type <text> on the console after <seconds-at> (repeatable)."), QStringLiteral("text"));
    const QCommandLineOption commandAtOption(QStringLiteral("command-at"), QStringLiteral("When to type the commands, in seconds (default 1)."), QStringLiteral("s"), QStringLiteral("1"));
    const QCommandLineOption frameOption(QStringLiteral("frame"), QStringLiteral("Save the display as a PNG <file> when stopping."), QStringLiteral("file"));
    parser.addOption(secondsOption);
    parser.addOption(ledsOption);
    parser.addOption(commandOption);
    parser.addOption(commandAtOption);
    const QCommandLineOption holdOption(QStringLiteral("hold"), QStringLiteral("Hold the keys named in <list> (comma-separated, as on the key test) from 3.5 s on, turn the knob 3 clicks."), QStringLiteral("list"));
    const QCommandLineOption batteryOption(QStringLiteral("battery"), QStringLiteral("Battery voltage in millivolts (default 7800)."), QStringLiteral("mv"));
    parser.addOption(frameOption);
    parser.addOption(holdOption);
    const QCommandLineOption pressOption(QStringLiteral("press"),
        QStringLiteral("Press and release the keys named in <list> (comma-separated, as on the key test) one after another, "
                       "from 3.5 s on, 0.4 s apart."),
        QStringLiteral("list"));
    parser.addOption(pressOption);
    parser.addOption(batteryOption);
    parser.process(app);

    static ConsoleLeds leds(parser.isSet(ledsOption));
    static ConsoleLog logSink;
    static SimSystem system;
    static SimDisplay display;
    static SimInput input;
    static SimBattery battery;
    if (parser.isSet(batteryOption)) {
        battery.setMillivolts(parser.value(batteryOption).toInt());
    }
    static SimStorage storage;
    static dmm::services::D878Uv2Calibration calibration;
    static dmm::services::D878Uv2Codeplug codeplug{storage};
    static SimAudio audio;
    static SimFm fm;
    static dmm::app::Platform platform{
        .leds = leds,
        .logSink = logSink,
        .system = system,
        .fm = fm,
        .boardName = "sim_headless",
        .display = &display,
        .input = &input,
        .battery = &battery,
        .storage = &storage,
        .audio = &audio,
        .calibration = &calibration,
        .codeplug = &codeplug,
    };
    dmm::app::start(platform);
    dmm::os::runScheduler();

    if (parser.isSet(holdOption)) {
        const QStringList names = parser.value(holdOption).split(QLatin1Char(','), Qt::SkipEmptyParts);
        QTimer::singleShot(3500, &app, [names] {
            for (unsigned i = 0; i < static_cast<unsigned>(dmm::services::Key::Count); ++i) {
                const auto key = static_cast<dmm::services::Key>(i);
                const std::string_view n = input.keyLabel(key);
                if (names.contains(QString::fromUtf8(n.data(), static_cast<qsizetype>(n.size())), Qt::CaseInsensitive)) {
                    input.setKey(key, true);
                }
            }
            input.turn(3);
            input.setVolume(734);
        });
    }
    if (parser.isSet(pressOption)) {
        const QStringList names = parser.value(pressOption).split(QLatin1Char(','), Qt::SkipEmptyParts);
        int at = 3500;
        for (const QString &name : names) {
            for (unsigned i = 0; i < static_cast<unsigned>(dmm::services::Key::Count); ++i) {
                const auto key = static_cast<dmm::services::Key>(i);
                const std::string_view n = input.keyLabel(key);
                if (name.compare(QString::fromUtf8(n.data(), static_cast<qsizetype>(n.size())), Qt::CaseInsensitive) == 0) {
                    QTimer::singleShot(at, &app, [key] { input.setKey(key, true); });
                    QTimer::singleShot(at + 150, &app, [key] { input.setKey(key, false); });
                    break;
                }
            }
            at += 400;
        }
    }
    const QStringList commands = parser.values(commandOption);
    if (!commands.isEmpty()) {
        QTimer::singleShot(static_cast<int>(parser.value(commandAtOption).toDouble() * 1000), &app, [commands] {
            for (const QString &c : commands) {
                logSink.addInput(c.toUtf8() + '\n');
            }
        });
    }
    const int seconds = parser.value(secondsOption).toInt();
    const QString frame = parser.value(frameOption);
    if (seconds > 0) {
        QTimer::singleShot(seconds * 1000, &app, [&app, frame] {
            dmm::os::requestStop();
            dmm::os::joinAll();
            if (!frame.isEmpty()) {
                display.visibleImage().save(frame);
            }
            app.quit();
        });
    }
    return QCoreApplication::exec();
}
