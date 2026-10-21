#include "app/AppController.h"
#include "app/ArtNetwork.h"
#include "app/Settings.h"
#include "app/TestDriver.h"
#include "player/MpvPlayer.h"

#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QFile>
#include <QFontDatabase>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>

#include <clocale>

Q_LOGGING_CATEGORY(lcStartup, "quince.startup")

int main(int argc, char *argv[])
{
    QElapsedTimer startup;
    startup.start();

    // QApplication for the tray icon and its menu
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("quince"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Quince"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("quince"));

    // Quince's own texts and Qt's standard ones (dialog buttons) in the system language
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("quince"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&translator);
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator);

    // libmpv parses numbers with the C locale; QGuiApplication has just set
    // LC_NUMERIC from the environment, which breaks it on e.g. de_DE.
    std::setlocale(LC_NUMERIC, "C");

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
    parser.addOption(aoOption);
    parser.addOption(playOption);
    parser.addOption(quitAfterOption);
    parser.addOption(screenshotOption);
    const QCommandLineOption stepsOption(QStringLiteral("steps"),
                                         QStringLiteral("Replay the input script in file (test runs)."),
                                         QStringLiteral("file"));
    const QCommandLineOption noSessionOption(QStringLiteral("no-session"),
                                             QStringLiteral("Do not restore the last queue."));
    const QCommandLineOption appIdOption(QStringLiteral("app-id"),
                                         QStringLiteral("Wayland app id / X11 class (default quince)."),
                                         QStringLiteral("id"));
    parser.addOption(stepsOption);
    parser.addOption(noSessionOption);
    parser.addOption(appIdOption);
    parser.process(app);
    if (parser.isSet(appIdOption))
        QGuiApplication::setDesktopFileName(parser.value(appIdOption));

    AppOptions options;
    options.audioOutput = parser.value(aoOption);
    options.playOnStart = parser.value(playOption);
    options.restoreSession = !parser.isSet(noSessionOption);

    QQuickStyle::setStyle(QStringLiteral("Material"));
    // Inter (OFL) everywhere, so the look does not depend on the system's fonts
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Quince/qml/fonts/InterVariable.ttf"));
    QFont font(QStringLiteral("Inter Variable"));
    font.setPixelSize(14);
    QGuiApplication::setFont(font);

    AppController controller(options);
    // 8 bits per channel: some EGL setups hand out 16-bit (RGB565) surfaces,
    // which turns the soft glows into visible bands
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRedBufferSize(8);
    format.setGreenBufferSize(8);
    format.setBlueBufferSize(8);
    // a see-through window needs an alpha channel before it is created
    if (controller.settings()->transparentWindow()) {
        format.setAlphaBufferSize(8);
        QQuickWindow::setDefaultAlphaBuffer(true);
    }
    QSurfaceFormat::setDefaultFormat(format);

    ArtNetworkFactory artNetwork;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&artNetwork);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Quince", "Main");
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        controller.setWindow(window);
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [startup] {
            static bool first = true;
            if (first)
                qCDebug(lcStartup) << "first frame after" << startup.elapsed() << "ms";
            first = false;
        });
    }

    if (parser.isSet(stepsOption)) {
        QFile script(parser.value(stepsOption));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (window && script.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(script.readAll()).split(u'\n');
            auto *driver = new TestDriver(window, &controller, lines, &app);
            driver->start();
        }
    }

    if (parser.isSet(quitAfterOption)) {
        const QString shot = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(quitAfterOption).toInt(), &app, [&engine, &controller, shot] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
            if (window && !shot.isEmpty())
                window->grabWindow().save(shot);
            const MpvPlayer *player = controller.player();
            qInfo("test: position %.1f duration %.1f paused %d idle %d", player->position(),
                  player->duration(), player->paused(), player->idle());
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
