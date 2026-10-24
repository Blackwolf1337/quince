#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class MpvPlayer;
class Playback;
class Settings;
class UiHost;
class QAction;
class QMenu;
class QSystemTrayIcon;

// The tray icon with the now playing line and the player's buttons.
class Tray : public QObject
{
    Q_OBJECT

public:
    Tray(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host, QObject *parent = nullptr);
    ~Tray() override;

    // the menu in the language just chosen
    void retranslate();
    // a bubble from the tray icon; false while there is no visible icon
    bool showMessage(const QString &title, const QString &body, const QImage &cover);

private:
    void applySettings();
    void create();
    void update();

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;
    UiHost *m_host = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_nowPlaying = nullptr;
    QAction *m_playPause = nullptr;
    QAction *m_next = nullptr;
    QAction *m_previous = nullptr;
    QAction *m_show = nullptr;
    QAction *m_quit = nullptr;
};
