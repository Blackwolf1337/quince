#include "app/TestDriver.h"
#include "app/AppController.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/Queue.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QWheelEvent>

namespace {

// pause between two commands, so the scene can react
constexpr int kStepMs = 150;

} // namespace

TestDriver::TestDriver(QQuickWindow *window, AppController *app, const QStringList &lines, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_app(app)
    , m_lines(lines)
{
}

void TestDriver::start()
{
    QTimer::singleShot(kStepMs, this, &TestDriver::next);
}

void TestDriver::next()
{
    while (m_pos < m_lines.size()) {
        const QString line = m_lines.at(m_pos++).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u"wait ")) {
            QTimer::singleShot(line.mid(5).toInt(), this, &TestDriver::next);
            return;
        }
        run(line);
        QTimer::singleShot(kStepMs, this, &TestDriver::next);
        return;
    }
}

void TestDriver::mouse(int x, int y, Qt::MouseButton button)
{
    const QPointF pos(x, y);
    const QPointF global = m_window->mapToGlobal(pos);
    const QPointingDevice *device = QPointingDevice::primaryPointingDevice();

    QMouseEvent move(QEvent::MouseMove, pos, pos, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier, device);
    QCoreApplication::sendEvent(m_window, &move);
    if (button == Qt::NoButton)
        return;
    QMouseEvent press(QEvent::MouseButtonPress, pos, pos, global, button, button, Qt::NoModifier, device);
    QCoreApplication::sendEvent(m_window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, global, button, Qt::NoButton, Qt::NoModifier, device);
    QCoreApplication::sendEvent(m_window, &release);
}

void TestDriver::key(int key, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(m_window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(m_window, &release);
}

void TestDriver::run(const QString &line)
{
    const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
    const QString command = parts.value(0);

    if (command == u"click" || command == u"move") {
        Qt::MouseButton button = Qt::NoButton;
        if (command == u"click")
            button = parts.value(3) == u"right" ? Qt::RightButton : Qt::LeftButton;
        mouse(parts.value(1).toInt(), parts.value(2).toInt(), button);
    } else if (command == u"scroll") {
        const QPointF pos(parts.value(1).toInt(), parts.value(2).toInt());
        const int dy = parts.value(3).toInt();
        QWheelEvent wheel(pos, m_window->mapToGlobal(pos), QPoint(0, dy), QPoint(0, dy), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window, &wheel);
    } else if (command == u"type") {
        // Qt key codes of printable ASCII are their upper case character codes
        for (const QChar c : line.mid(5))
            key(c.unicode() < 128 ? c.toUpper().unicode() : 0, QString(c));
    } else if (command == u"key") {
        static const QHash<QString, int> keys{
            {QStringLiteral("Return"), Qt::Key_Return}, {QStringLiteral("Escape"), Qt::Key_Escape},
            {QStringLiteral("Down"), Qt::Key_Down},     {QStringLiteral("Up"), Qt::Key_Up},
            {QStringLiteral("Space"), Qt::Key_Space},   {QStringLiteral("Backspace"), Qt::Key_Backspace},
            {QStringLiteral("Tab"), Qt::Key_Tab},
        };
        const int k = keys.value(parts.value(1));
        key(k, k == Qt::Key_Space ? QStringLiteral(" ") : k == Qt::Key_Return ? QStringLiteral("\r") : QString());
    } else if (command == u"shot") {
        m_window->grabWindow().save(parts.value(1));
    } else if (command == u"state") {
        const Playback *playback = m_app->playback();
        const MpvPlayer *player = m_app->player();
        qInfo().noquote() << "state:" << playback->track().value(QStringLiteral("title")).toString()
                          << "| queue" << playback->queue()->current() << "/" << playback->queue()->count()
                          << "| pos" << player->position() << "/" << player->duration()
                          << "| paused" << player->paused() << "idle" << player->idle()
                          << "| loading" << playback->loading() << "| error" << playback->error()
                          << "|" << playback->streamInfo();
    } else if (command == u"open") {
        m_app->openBrowse(parts.value(1));
    } else if (command == u"size") {
        qInfo() << "window" << m_window->size() << "dpr" << m_window->devicePixelRatio()
                << "content" << m_window->contentItem()->size();
    } else if (command == u"sh") {
        // a shell command in the app's environment (e.g. playerctl on its bus)
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::MergedChannels);
        const QString shellLine = line.mid(3);
        connect(process, &QProcess::finished, this, [process, shellLine] {
            qInfo().noquote() << "sh:" << shellLine << "->" << QString::fromUtf8(process->readAll()).trimmed();
            process->deleteLater();
        });
        process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), shellLine});
    } else if (command == u"quit") {
        QCoreApplication::quit();
    } else {
        qWarning() << "test driver: unknown command" << line;
    }
}
