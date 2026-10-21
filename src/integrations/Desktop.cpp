#include "integrations/Desktop.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>

#ifdef QUINCE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace {

const QString kIcon = QStringLiteral(":/qt/qml/Quince/qml/icons/quince.svg");
constexpr int kNotificationMs = 5000;
// a cover that arrives later than this after the track change is not waited for
constexpr int kCoverWaitMs = 1500;

QString artistsOf(const QVariantMap &track)
{
    QString text = track.value(QStringLiteral("artistText")).toString();
    if (text.isEmpty()) {
        QStringList names;
        for (const QVariant &a : track.value(QStringLiteral("artists")).toList())
            names.append(a.toMap().value(QStringLiteral("name")).toString());
        text = names.join(QStringLiteral(", "));
    }
    const QString album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    return album.isEmpty() ? text : text + QStringLiteral(" • ") + album;
}

} // namespace

Desktop::Desktop(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
{
    connect(m_settings, &Settings::changed, this, &Desktop::applySettings);
    connect(m_playback, &Playback::trackChanged, this, &Desktop::updateTray);
    connect(m_player, &MpvPlayer::pausedChanged, this, &Desktop::updateTray);
    // the notification again when playback goes on (Settings)
    connect(m_player, &MpvPlayer::pausedChanged, this, [this] {
        if (!m_player->paused() && !m_player->idle() && m_settings->notifyOnResume()) {
            m_notifiedId.clear();
            notifyTrack();
        }
    });
    // a moment for the cover, then the notification goes out with or without it
    connect(m_playback, &Playback::trackChanged, this, [this] {
        QTimer::singleShot(kCoverWaitMs, this, &Desktop::notifyTrack);
    });
    applySettings();
}

Desktop::~Desktop()
{
    delete m_menu;
}

void Desktop::setWindow(QWindow *window)
{
    m_window = window;
}

void Desktop::setCover(const QString &videoId, const QImage &cover)
{
    m_coverId = videoId;
    m_cover = cover;
    if (videoId == m_playback->videoId())
        notifyTrack();
}

void Desktop::applySettings()
{
    const bool want = m_settings->tray() && QSystemTrayIcon::isSystemTrayAvailable();
    if (want && !m_tray)
        createTray();
    if (m_tray)
        m_tray->setVisible(want);
}

void Desktop::createTray()
{
    m_menu = new QMenu();
    m_nowPlaying = m_menu->addAction(QString());
    m_nowPlaying->setEnabled(false);
    m_menu->addSeparator();
    m_playPause = m_menu->addAction(QString(), m_player, &MpvPlayer::togglePause);
    m_next = m_menu->addAction(QString(), m_playback, &Playback::next);
    m_previous = m_menu->addAction(QString(), m_playback, &Playback::previous);
    m_menu->addSeparator();
    m_show = m_menu->addAction(QString(), this, &Desktop::toggleWindow);
    m_quit = m_menu->addAction(QString(), qApp, &QCoreApplication::quit);

    m_tray = new QSystemTrayIcon(QIcon(kIcon), this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && m_settings->trayClickPlays())
            m_player->togglePause();
        else if (reason == QSystemTrayIcon::Trigger)
            toggleWindow();
        else if (reason == QSystemTrayIcon::MiddleClick)
            m_player->togglePause();
    });
    retranslate();
}

void Desktop::retranslate()
{
    if (!m_menu)
        return;
    m_next->setText(tr("Next"));
    m_previous->setText(tr("Previous"));
    m_show->setText(tr("Show or hide Quince"));
    m_quit->setText(tr("Quit"));
    updateTray();
}

void Desktop::updateTray()
{
    if (!m_tray)
        return;
    const QVariantMap track = m_playback->track();
    const QString title = track.value(QStringLiteral("title")).toString();
    const QString line = title.isEmpty() ? tr("Nothing playing") : title + QStringLiteral(" - ") + artistsOf(track);
    m_nowPlaying->setText(line);
    m_tray->setToolTip(title.isEmpty() ? QStringLiteral("Quince") : line);
    m_playPause->setText(m_player->paused() || m_player->idle() ? tr("Play") : tr("Pause"));
}

void Desktop::toggleWindow()
{
    if (!m_window)
        return;
    if (m_window->isVisible() && m_window->isActive()) {
        m_window->hide();
        return;
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

// While Quince is in front, the user sees the change anyway.
void Desktop::notifyTrack()
{
    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    if (!m_settings->notifications() || videoId.isEmpty() || videoId == m_notifiedId)
        return;
    if (m_window && m_window->isVisible() && m_window->isActive())
        return;
    m_notifiedId = videoId;
    sendNotification(track.value(QStringLiteral("title")).toString(), artistsOf(track),
                     m_coverId == videoId ? m_cover : QImage());
}

void Desktop::sendNotification(const QString &title, const QString &body, const QImage &cover)
{
#ifdef QUINCE_DBUS
    // org.freedesktop.Notifications: replaces the bubble of the last track
    QString imagePath;
    if (!cover.isNull()) {
        imagePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cover.png");
        QDir().mkpath(QFileInfo(imagePath).absolutePath());
        cover.save(imagePath);
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    const QString urgency = m_settings->notificationUrgency();
    QVariantMap hints{
        {QStringLiteral("desktop-entry"), QStringLiteral("quince")},
        {QStringLiteral("transient"), true},
        // the spec's byte: 0 low, 1 normal, 2 critical
        {QStringLiteral("urgency"), QVariant::fromValue(uchar(urgency == u"low" ? 0 : urgency == u"critical" ? 2 : 1))},
    };
    if (!imagePath.isEmpty())
        hints.insert(QStringLiteral("image-path"), imagePath);
    call << QStringLiteral("Quince") << m_notificationId << QString() << title << body.toHtmlEscaped()
         << QStringList() << hints << kNotificationMs;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isValid())
            m_notificationId = reply.value();
    });
#else
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(title, body, cover.isNull() ? QIcon(kIcon) : QIcon(QPixmap::fromImage(cover)),
                            kNotificationMs);
#endif
}
