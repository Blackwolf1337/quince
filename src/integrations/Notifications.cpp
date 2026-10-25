#include "integrations/Notifications.h"
#include "app/Settings.h"
#include "app/UiHost.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "ytm/Parser.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

#ifdef QUINCE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace {

#ifdef QUINCE_DBUS
constexpr int kNotificationMs = 5000;
#endif
// a cover that arrives later than this after the track change is not waited for
constexpr int kCoverWaitMs = 1500;

} // namespace

QString Notifications::artistsOf(const QVariantMap &track)
{
    const QString text = ytm::artistNames(track).join(QStringLiteral(", "));
    const QString album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    return album.isEmpty() ? text : text + QStringLiteral(" • ") + album;
}

Notifications::Notifications(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host,
                             QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
    , m_host(host)
{
    // the notification again when playback goes on (Settings)
    connect(m_player, &MpvPlayer::pausedChanged, this, [this] {
        if (!m_player->paused() && !m_player->idle() && m_settings->notifyOnResume()) {
            m_notifiedId.clear();
            notifyTrack();
        }
    });
    // a moment for the cover, then the notification goes out with or without it
    connect(m_playback, &Playback::trackChanged, this, [this] {
        QTimer::singleShot(kCoverWaitMs, this, &Notifications::notifyTrack);
    });
}

void Notifications::setCover(const QString &videoId, const QImage &cover)
{
    m_coverId = videoId;
    m_cover = cover;
    if (videoId == m_playback->videoId())
        notifyTrack();
}

// While Quince is in front, the user sees the change anyway.
void Notifications::notifyTrack()
{
    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    if (!m_settings->notifications() || videoId.isEmpty() || videoId == m_notifiedId)
        return;
    if (m_host && m_host->windowInFront())
        return;
    m_notifiedId = videoId;
    sendNotification(track.value(QStringLiteral("title")).toString(), artistsOf(track),
                     m_coverId == videoId ? m_cover : QImage());
}

void Notifications::sendNotification(const QString &title, const QString &body, const QImage &cover)
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
    if (m_host)
        m_host->showNotification(title, body, cover);
#endif
}
