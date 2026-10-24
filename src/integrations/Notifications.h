#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>

class MpvPlayer;
class Playback;
class Settings;
class UiHost;

// Track change notifications: D-Bus on Linux, the frontend's way elsewhere.
class Notifications : public QObject
{
    Q_OBJECT

public:
    Notifications(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host,
                  QObject *parent = nullptr);

    // the cover of the playing track, decoded once by the color theme
    void setCover(const QString &videoId, const QImage &cover);

    // "artists • album" of a track, also the tray's line
    static QString artistsOf(const QVariantMap &track);

private:
    void notifyTrack();
    void sendNotification(const QString &title, const QString &body, const QImage &cover);

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;
    UiHost *m_host = nullptr;

    QString m_notifiedId;
    QString m_coverId;
    QImage m_cover;
    uint m_notificationId = 0; // the bubble a new track replaces
};
