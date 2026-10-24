#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class Account;
class AudioEffects;
class Downloader;
class SponsorBlock;
class AccountCookies;
class ColorTheme;
class Notifications;
class Discord;
class Lyrics;
class Mpris;
class InnerTube;
class JsSolver;
class Language;
class MpvPlayer;
class Playback;
class Settings;
class StreamResolver;
class UiHost;

struct AppOptions
{
    QString audioOutput; // mpv --ao, "null" for silent runs
    QString playOnStart; // video id or link
    bool restoreSession = true;
};

// Owns the player and every service. The QML frontend registers it as the
// singleton "App" (qml/Registrations.h).
class AppController : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("player/AudioEffects.h")
    Q_MOC_INCLUDE("download/Downloader.h")
    Q_MOC_INCLUDE("integrations/SponsorBlock.h")
    Q_MOC_INCLUDE("player/MpvPlayer.h")
    Q_MOC_INCLUDE("player/Playback.h")
    Q_MOC_INCLUDE("ytm/Account.h")
    Q_MOC_INCLUDE("app/Settings.h")
    Q_MOC_INCLUDE("app/ColorTheme.h")
    Q_MOC_INCLUDE("lyrics/Lyrics.h")

    Q_PROPERTY(MpvPlayer *player READ player CONSTANT)
    Q_PROPERTY(Playback *playback READ playback CONSTANT)
    Q_PROPERTY(Account *account READ account CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(ColorTheme *theme READ theme CONSTANT)
    Q_PROPERTY(Lyrics *lyrics READ lyrics CONSTANT)
    Q_PROPERTY(AudioEffects *effects READ effects CONSTANT)
    Q_PROPERTY(Downloader *downloads READ downloads CONSTANT)
    Q_PROPERTY(SponsorBlock *sponsorBlock READ sponsorBlock CONSTANT)

public:
    // host: the frontend's window, clipboard and notifications (outlives this)
    AppController(const AppOptions &options, UiHost *host, QObject *parent = nullptr);
    ~AppController() override;

    static AppController *instance() { return s_instance; }

    MpvPlayer *player() const { return m_player; }
    Playback *playback() const { return m_playback; }
    Account *account() const { return m_account; }
    Settings *settings() const { return m_settings; }
    ColorTheme *theme() const { return m_theme; }
    Lyrics *lyrics() const { return m_lyrics; }
    AudioEffects *effects() const { return m_effects; }
    Downloader *downloads() const { return m_downloads; }
    SponsorBlock *sponsorBlock() const { return m_sponsorBlock; }

    InnerTube *innerTube() const { return m_innerTube; }
    // the language of the texts; changed() follows the settings
    Language *language() const { return m_language; }

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
    // the music.youtube.com link of a track, album, playlist or artist to the clipboard
    Q_INVOKABLE void copyLink(const QVariantMap &item);

signals:
    // QML pushes a page for target {browseId, params}
    void navigateRequested(const QVariantMap &target);
    void dialogRequested(const QString &name, const QVariantMap &data);
    void languageChanged();
    // short feedback for the toast
    void message(const QString &text);

private:
    void applySettings();

    static AppController *s_instance;

    MpvPlayer *m_player = nullptr;
    InnerTube *m_innerTube = nullptr;
    JsSolver *m_solver = nullptr;
    StreamResolver *m_resolver = nullptr;
    Playback *m_playback = nullptr;
    Settings *m_settings = nullptr;
    Language *m_language = nullptr;
    AccountCookies *m_cookies = nullptr;
    Account *m_account = nullptr;
    ColorTheme *m_theme = nullptr;
    Lyrics *m_lyrics = nullptr;
    AudioEffects *m_effects = nullptr;
    Downloader *m_downloads = nullptr;
    SponsorBlock *m_sponsorBlock = nullptr;
    UiHost *m_host = nullptr;
    Notifications *m_notifications = nullptr;
    Discord *m_discord = nullptr;
    Mpris *m_mpris = nullptr; // Linux only
    QString m_reportedPlay; // video whose play went into the history last
};
