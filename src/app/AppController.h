#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class Account;
class AccountCookies;
class ColorTheme;
class Desktop;
class Discord;
class Lyrics;
class Mpris;
class QWindow;
class InnerTube;
class MpvPlayer;
class Playback;
class Settings;
class StreamResolver;
class QJSEngine;
class QQmlEngine;

struct AppOptions
{
    QString audioOutput; // mpv --ao, "null" for silent runs
    QString playOnStart; // video id or link
    bool restoreSession = true;
};

// Owns the player and every service, and is the QML singleton "App".
class AppController : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("player/MpvPlayer.h")
    Q_MOC_INCLUDE("player/Playback.h")
    Q_MOC_INCLUDE("ytm/Account.h")
    Q_MOC_INCLUDE("app/Settings.h")
    Q_MOC_INCLUDE("app/ColorTheme.h")
    Q_MOC_INCLUDE("lyrics/Lyrics.h")
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(MpvPlayer *player READ player CONSTANT)
    Q_PROPERTY(Playback *playback READ playback CONSTANT)
    Q_PROPERTY(Account *account READ account CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(ColorTheme *theme READ theme CONSTANT)
    Q_PROPERTY(Lyrics *lyrics READ lyrics CONSTANT)

public:
    explicit AppController(const AppOptions &options, QObject *parent = nullptr);
    ~AppController() override;

    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);
    static AppController *instance() { return s_instance; }

    MpvPlayer *player() const { return m_player; }
    Playback *playback() const { return m_playback; }
    Account *account() const { return m_account; }
    Settings *settings() const { return m_settings; }
    ColorTheme *theme() const { return m_theme; }
    Lyrics *lyrics() const { return m_lyrics; }

    // the main window, for the tray icon
    void setWindow(QWindow *window);
    InnerTube *innerTube() const { return m_innerTube; }

    // A click on an item: plays it or opens its page.
    Q_INVOKABLE void open(const QVariantMap &item);
    Q_INVOKABLE void openBrowse(const QString &browseId, const QString &params = {});

    // Volume from a slider position (0..100) through the settings' curve.
    Q_INVOKABLE void setVolumeSlider(double slider);

    // Cover URL in the size the view needs (device pixels).
    Q_INVOKABLE QString art(const QString &url, int pixels) const;

    // Opens a dialog of the main window (signin, addToPlaylist) from places
    // that do not live long enough to own it, like a context menu.
    Q_INVOKABLE void requestDialog(const QString &name, const QVariantMap &data = {});

signals:
    // QML pushes a page for target {browseId, params}
    void navigateRequested(const QVariantMap &target);
    void dialogRequested(const QString &name, const QVariantMap &data);

private:
    void applySettings();

    static AppController *s_instance;

    MpvPlayer *m_player = nullptr;
    StreamResolver *m_resolver = nullptr;
    InnerTube *m_innerTube = nullptr;
    Playback *m_playback = nullptr;
    Settings *m_settings = nullptr;
    AccountCookies *m_cookies = nullptr;
    Account *m_account = nullptr;
    ColorTheme *m_theme = nullptr;
    Lyrics *m_lyrics = nullptr;
    Desktop *m_desktop = nullptr;
    Discord *m_discord = nullptr;
    Mpris *m_mpris = nullptr; // Linux only
    QString m_reportedPlay; // video whose play went into the history last
};
