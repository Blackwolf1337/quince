// The Slint frontend. Slint keeps the main thread (winit needs it on macOS);
// the core (QCoreApplication, AppController and everything it owns) runs on a
// thread of its own. Bridge.h has the rules for crossing between the two.

#include "Bindings.h"
#include "Frost.h"
#include "ImageCache.h"
#include "SlintHost.h"
#include "TestDriver.h"
#include "VideoRenderer.h"
#include "app/AppController.h"
#include "app/CommandLine.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <chrono>
#include <clocale>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

namespace {

// how often the Slint thread tells the core whether the window is in front
constexpr auto kWindowStateInterval = std::chrono::seconds(1);

// The core's thread and its event loop, and a way to end it from the Slint
// thread at any time, also before the loop runs.
class CoreThread
{
public:
    template<typename Run>
    void start(Run run)
    {
        m_thread = std::thread([this, run] { run(*this); });
    }

    // core thread: the loop, unless quit() came first
    void exec(QCoreApplication &app)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_quit)
                return;
            m_app = &app;
        }
        app.exec();
        std::lock_guard lock(m_mutex);
        m_app = nullptr;
    }

    // Slint thread: ends the loop and waits for the thread (the session is
    // saved on the way out)
    void quit()
    {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
            if (m_app)
                QMetaObject::invokeMethod(m_app, &QCoreApplication::quit, Qt::QueuedConnection);
        }
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    std::thread m_thread;
    std::mutex m_mutex;
    QCoreApplication *m_app = nullptr;
    bool m_quit = false;
};

} // namespace

int main(int argc, char *argv[])
{
    // the window first: it shows while the core starts. Its last size comes
    // from the settings now, before it exists (a set_size before winit made
    // the window crashed it on Windows); the core thread does not run yet
    QCoreApplication::setApplicationName(QStringLiteral("quince"));
    const QSize startSize = Settings().savedWindowSize();
    // the Wayland app id / X11 class: desktops group, list and pick windows
    // by it (none at all without); before the window, so not from
    // parseCommandLine on the core thread
    std::string appId = "quince";
    for (int i = 1; i < argc; i++) {
        const std::string_view arg = argv[i];
        if (arg.starts_with("--app-id="))
            appId = arg.substr(9);
        else if (arg == "--app-id" && i + 1 < argc)
            appId = argv[i + 1];
    }
    slint::set_xdg_app_id(appId);
    std::optional<slint::ComponentHandle<ui::MainWindow>> window(ui::MainWindow::create());
    if (startSize.width() > 0 && startSize.height() > 0) {
        (*window)->set_start_width(float(startSize.width()));
        (*window)->set_start_height(float(startSize.height()));
    }
    std::optional<slint::ComponentHandle<ui::TrayIcon>> tray(ui::TrayIcon::create());
    const bridge::Window weakWindow(*window);
    const bridge::Tray weakTray(*tray);

    VideoRenderer video(weakWindow);
    if ((*window)->window().set_rendering_notifier(std::ref(video)))
        std::fprintf(stderr, "quince: Slint renders without OpenGL here, no video\n");
    SlintHost host(weakWindow);
    startFrost(*window);
    ImageCache images;

    CoreThread core;
    core.start([&](CoreThread &self) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("quince"));
        QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
        // libmpv parses numbers with the C locale; QCoreApplication has just
        // set LC_NUMERIC from the environment, which breaks it on e.g. de_DE
        std::setlocale(LC_NUMERIC, "C");
        const CommandLine line = parseCommandLine(app);

        AppController controller(line.app, &host);
        images.startCore(&app);
        bindCore(controller, weakWindow, weakTray, &images, &video, &host);

        if (!line.steps.isEmpty()) {
            QFile script(line.steps);
            if (script.open(QIODevice::ReadOnly)) {
                const QStringList lines = QString::fromUtf8(script.readAll()).split(u'\n');
                (new TestDriver(weakWindow, &controller, lines, &app))->start();
            }
        }
        if (line.quitAfterMs >= 0) {
            QTimer::singleShot(line.quitAfterMs, &app, [&app, weakWindow, shot = line.screenshot, &controller] {
                const MpvPlayer *player = controller.player();
                qInfo("test: position %.1f duration %.1f paused %d idle %d", player->position(), player->duration(),
                      player->paused(), player->idle());
                bridge::toUi(weakWindow, [shot, &app](ui::MainWindow &w) {
                    if (!shot.isEmpty())
                        TestDriver::screenshot(w, shot, &app);
                    slint::quit_event_loop();
                });
            });
        }
        self.exec(app);
    });

    (*window)->window().on_close_requested([&] {
        if ((*window)->global<ui::App>().get_close_to_tray())
            host.setWindowState(false, false);
        else
            slint::quit_event_loop();
        return slint::CloseRequestResponse::HideWindow;
    });
    // the window's state for the core, and its size once it stayed the same
    // for a tick (a maximized window keeps the size it had before)
    slint::Timer windowState;
    slint::LogicalSize lastSize({ 0.f, 0.f }), savedSize({ 0.f, 0.f });
    windowState.start(slint::TimerMode::Repeated, kWindowStateInterval, [&] {
        const slint::Window &w = (*window)->window();
        host.setWindowState(w.is_visible(), w.is_minimized());
        const float scale = w.scale_factor();
        const slint::LogicalSize size({ float(w.size().width) / scale, float(w.size().height) / scale });
        if (w.is_visible() && !w.is_maximized() && !w.is_minimized() && size.width == lastSize.width
            && size.height == lastSize.height && (size.width != savedSize.width || size.height != savedSize.height)) {
            savedSize = size;
            (*window)->global<ui::App>().invoke_save_window_size(int(size.width), int(size.height));
        }
        lastSize = size;
    });

    (*window)->show();
    host.setWindowState(true, false);
    slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);

    // Slint's objects go first, on this thread: the window takes the mpv
    // render context with it while the core and its players still run
    windowState.stop();
    bridge::runUiShutdown();
    (*window)->hide();
    window.reset();
    tray.reset();
    images.clear();
    core.quit();
    return 0;
}
