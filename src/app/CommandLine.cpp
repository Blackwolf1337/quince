#include "app/CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>

CommandLine parseCommandLine(const QCoreApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native YouTube Music player"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption aoOption(QStringLiteral("ao"),
                                      QStringLiteral("mpv audio output driver (null for silence)."),
                                      QStringLiteral("driver"));
    const QCommandLineOption playOption(QStringLiteral("play"),
                                        QStringLiteral("Video id or link to play on start."),
                                        QStringLiteral("id"));
    const QCommandLineOption quitAfterOption(QStringLiteral("quit-after"),
                                             QStringLiteral("Quit after this many milliseconds (test runs)."),
                                             QStringLiteral("ms"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save a picture of the window before quitting (test runs)."),
                                              QStringLiteral("file"));
    const QCommandLineOption stepsOption(QStringLiteral("steps"),
                                         QStringLiteral("Replay the input script in file (test runs)."),
                                         QStringLiteral("file"));
    const QCommandLineOption noSessionOption(QStringLiteral("no-session"),
                                             QStringLiteral("Do not restore the last queue."));
    const QCommandLineOption appIdOption(QStringLiteral("app-id"),
                                         QStringLiteral("Wayland app id / X11 class (default quince)."),
                                         QStringLiteral("id"));
    parser.addOption(aoOption);
    parser.addOption(playOption);
    parser.addOption(quitAfterOption);
    parser.addOption(screenshotOption);
    parser.addOption(stepsOption);
    parser.addOption(noSessionOption);
    parser.addOption(appIdOption);
    parser.process(app);

    CommandLine line;
    line.app.audioOutput = parser.value(aoOption);
    line.app.playOnStart = parser.value(playOption);
    line.app.restoreSession = !parser.isSet(noSessionOption);
    if (parser.isSet(quitAfterOption))
        line.quitAfterMs = parser.value(quitAfterOption).toInt();
    line.screenshot = parser.value(screenshotOption);
    line.steps = parser.value(stepsOption);
    line.appId = parser.value(appIdOption);
    return line;
}
