// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

// Desktop simulator: runs the app on the PC and shows its LEDs, display
// and log in a window.

#include "dmm/app/app.h"
#include "dmm/os/os.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_codeplug.h"
#include "sim_audio.h"
#include "sim_bridge.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("simqt"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DeltaMM simulator"));
    parser.addHelpOption();
    const QCommandLineOption secondsOption(QStringLiteral("seconds"), QStringLiteral("Quit after <n> seconds (0 = run until closed)."), QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Save the window to <file> just before quitting (needs --seconds)."), QStringLiteral("file"));
    parser.addOption(secondsOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    SimBridge *bridge = SimBridge::instance();
    static SimAudio audio;
    static dmm::services::D878Uv2Calibration calibration;
    static dmm::services::D878Uv2Codeplug codeplug{bridge->storage()};
    static dmm::app::Platform platform{*bridge, *bridge, *bridge, "sim_qt", nullptr, &bridge->display(), &bridge->input(), &bridge->battery(), &bridge->storage(), nullptr, nullptr, nullptr, &audio, &calibration, &codeplug};

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("DeltaMMSim", "Main");

    dmm::app::start(platform);
    dmm::os::runScheduler();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        dmm::os::requestStop();
        dmm::os::joinAll();
    });
    const int seconds = parser.value(secondsOption).toInt();
    if (seconds > 0) {
        const QString screenshot = parser.value(screenshotOption);
        QTimer::singleShot(seconds * 1000, &app, [&engine, screenshot] {
            if (!screenshot.isEmpty() && !engine.rootObjects().isEmpty()) {
                if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
                    window->grabWindow().save(screenshot);
                }
            }
            QCoreApplication::quit();
        });
    }
    return QGuiApplication::exec();
}
