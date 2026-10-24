#include "app/AppController.h"
#include "app/CommandLine.h"
#include "qml/ArtNetwork.h"
#include "app/Settings.h"
#include "qml/TestDriver.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "qml/QmlHost.h"
#include "qml/Tray.h"

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

    // libmpv parses numbers with the C locale; QGuiApplication has just set
    // LC_NUMERIC from the environment, which breaks it on e.g. de_DE.
    std::setlocale(LC_NUMERIC, "C");

    const CommandLine line = parseCommandLine(app);
    if (!line.appId.isEmpty())
        QGuiApplication::setDesktopFileName(line.appId);

    QQuickStyle::setStyle(QStringLiteral("Material"));
    // mpv draws video through OpenGL (MpvVideoItem); the default elsewhere is
    // Direct3D or Metal. Test runs without a GPU keep the software renderer.
    // QUINCE_GL=1 asks for OpenGL offscreen too (test runs that draw video)
    if ((QGuiApplication::platformName() != u"offscreen" || qEnvironmentVariableIntValue("QUINCE_GL"))
        && qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    // Inter (OFL) everywhere, so the look does not depend on the system's fonts
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Quince/qml/fonts/InterVariable.ttf"));
    QFont font(QStringLiteral("Inter Variable"));
    font.setPixelSize(14);
    QGuiApplication::setFont(font);

    // window, clipboard and notifications for the core; the window comes later
    QmlHost host;
    AppController controller(line.app, &host);
    Tray tray(controller.playback(), controller.player(), controller.settings(), &host);
    host.setTray(&tray);
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
    // a new language in the settings: every qsTr() binding again
    QObject::connect(&controller, &AppController::languageChanged, &engine, &QQmlEngine::retranslate);
    QObject::connect(&controller, &AppController::languageChanged, &tray, &Tray::retranslate);
    engine.loadFromModule("Quince", "Main");
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        host.setWindow(window);
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [startup] {
            static bool first = true;
            if (first)
                qCDebug(lcStartup) << "first frame after" << startup.elapsed() << "ms";
            first = false;
        });
    }

    if (!line.steps.isEmpty()) {
        QFile script(line.steps);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (window && script.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(script.readAll()).split(u'\n');
            auto *driver = new TestDriver(window, &controller, lines, &app);
            driver->start();
        }
    }

    if (line.quitAfterMs >= 0) {
        const QString shot = line.screenshot;
        QTimer::singleShot(line.quitAfterMs, &app, [&engine, &controller, shot] {
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
