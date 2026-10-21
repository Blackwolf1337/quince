#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

class MpvPlayer;
class Playback;
class Settings;
class QAction;
class QMenu;
class QSystemTrayIcon;
class QWindow;

// Tray icon and track change notifications.
class Desktop : public QObject
{
    Q_OBJECT

public:
    Desktop(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent = nullptr);
    ~Desktop() override;

    void setWindow(QWindow *window);
    // the cover of the playing track, decoded once by the color theme
    void setCover(const QString &videoId, const QImage &cover);

private:
    void applySettings();
    void createTray();
    void updateTray();
    void toggleWindow();
    void notifyTrack();
    void sendNotification(const QString &title, const QString &body, const QImage &cover);

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;
    QPointer<QWindow> m_window;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_nowPlaying = nullptr;
    QAction *m_playPause = nullptr;

    QString m_notifiedId;
    QString m_coverId;
    QImage m_cover;
    uint m_notificationId = 0; // the bubble a new track replaces
};
