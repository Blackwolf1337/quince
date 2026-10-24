#pragma once

#include <QObject>
#include <QSettings>
#include <QSize>
#include <QString>

// User settings, kept in quince.conf in the config folder.
class Settings : public QObject
{
    Q_OBJECT

    // the slider position, 0..100; with exponentialVolume the heard volume is (slider/100)^3
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool exponentialVolume READ exponentialVolume WRITE setExponentialVolume NOTIFY changed)
    // slider steps of the mouse wheel and the volume keys, in percent
    Q_PROPERTY(double volumeStep READ volumeStep WRITE setVolumeStep NOTIFY changed)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY changed)
    Q_PROPERTY(bool normalizeLoudness READ normalizeLoudness WRITE setNormalizeLoudness NOTIFY changed)
    Q_PROPERTY(bool albumColors READ albumColors WRITE setAlbumColors NOTIFY changed)
    // percent: the album color's glows (100 as designed), the video's ambient
    // light and its reach, the see-through window's background
    Q_PROPERTY(int glowStrength READ glowStrength WRITE setGlowStrength NOTIFY changed)
    Q_PROPERTY(bool coloredProgress READ coloredProgress WRITE setColoredProgress NOTIFY changed)
    Q_PROPERTY(int ambientStrength READ ambientStrength WRITE setAmbientStrength NOTIFY changed)
    Q_PROPERTY(int ambientSize READ ambientSize WRITE setAmbientSize NOTIFY changed)
    Q_PROPERTY(int windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY changed)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
    // also when playback goes on after a pause; "low", "normal" or "critical"
    Q_PROPERTY(bool notifyOnResume READ notifyOnResume WRITE setNotifyOnResume NOTIFY changed)
    Q_PROPERTY(QString notificationUrgency READ notificationUrgency WRITE setNotificationUrgency NOTIFY changed)
    Q_PROPERTY(bool tray READ tray WRITE setTray NOTIFY changed)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY changed)
    // a click on the tray icon plays or pauses instead of showing the window
    Q_PROPERTY(bool trayClickPlays READ trayClickPlays WRITE setTrayClickPlays NOTIFY changed)
    Q_PROPERTY(bool discord READ discord WRITE setDiscord NOTIFY changed)
    Q_PROPERTY(QString discordAppId READ discordAppId WRITE setDiscordAppId NOTIFY changed)
    Q_PROPERTY(bool lyricsLrclib READ lyricsLrclib WRITE setLyricsLrclib NOTIFY changed)
    // Linux: shortcuts through the desktop portal, read at start
    Q_PROPERTY(bool globalShortcuts READ globalShortcuts WRITE setGlobalShortcuts NOTIFY changed)
    Q_PROPERTY(bool skipDisliked READ skipDisliked WRITE setSkipDisliked NOTIFY changed)
    Q_PROPERTY(bool startPaused READ startPaused WRITE setStartPaused NOTIFY changed)
    // video-toggle, the highest picture (360 ... 2160) and the caption language ("" none)
    Q_PROPERTY(bool videoMode READ videoMode WRITE setVideoMode NOTIFY changed)
    Q_PROPERTY(int videoHeight READ videoHeight WRITE setVideoHeight NOTIFY changed)
    Q_PROPERTY(QString captionLanguage READ captionLanguage WRITE setCaptionLanguage NOTIFY changed)
    // visualizer style in the Now Playing view ("" off): bars, wave, spectrum, scope
    Q_PROPERTY(QString visualizer READ visualizer WRITE setVisualizer NOTIFY changed)
    // downloads: opus, m4a or mp3; the folder they go into
    Q_PROPERTY(QString downloadFormat READ downloadFormat WRITE setDownloadFormat NOTIFY changed)
    Q_PROPERTY(QString downloadFolder READ downloadFolder WRITE setDownloadFolder NOTIFY changed)
    // MP3 bit rate in kbit/s; files already there are not downloaded again;
    // at most this many tracks of a playlist (0 all of them)
    Q_PROPERTY(int downloadMp3Kbps READ downloadMp3Kbps WRITE setDownloadMp3Kbps NOTIFY changed)
    Q_PROPERTY(bool downloadSkipExisting READ downloadSkipExisting WRITE setDownloadSkipExisting NOTIFY changed)
    Q_PROPERTY(int downloadPlaylistMax READ downloadPlaylistMax WRITE setDownloadPlaylistMax NOTIFY changed)
    Q_PROPERTY(bool lyricsGenius READ lyricsGenius WRITE setLyricsGenius NOTIFY changed)
    Q_PROPERTY(bool lyricsMegalobiz READ lyricsMegalobiz WRITE setLyricsMegalobiz NOTIFY changed)
    // the lyrics source asked first ("" the usual order): lrclib, megalobiz, youtube, genius
    Q_PROPERTY(QString lyricsPreferred READ lyricsPreferred WRITE setLyricsPreferred NOTIFY changed)
    // a looser search when the exact lookup finds nothing (the length or album may differ)
    Q_PROPERTY(bool lyricsInexact READ lyricsInexact WRITE setLyricsInexact NOTIFY changed)
    // shown on the empty lines of synced lyrics
    Q_PROPERTY(QString lyricsGapText READ lyricsGapText WRITE setLyricsGapText NOTIFY changed)
    // Latin letters under Japanese, Korean and Chinese lines
    Q_PROPERTY(bool lyricsRomanize READ lyricsRomanize WRITE setLyricsRomanize NOTIFY changed)
    // Chinese lyrics in "hant" (Traditional) or "hans" (Simplified) characters, "" as they come
    Q_PROPERTY(QString lyricsChinese READ lyricsChinese WRITE setLyricsChinese NOTIFY changed)
    // how the sung line stands out: fancy, scale, offset or focus (Pear's line effects)
    Q_PROPERTY(QString lyricsEffect READ lyricsEffect WRITE setLyricsEffect NOTIFY changed)
    Q_PROPERTY(bool lyricsTimeCodes READ lyricsTimeCodes WRITE setLyricsTimeCodes NOTIFY changed)
    // "" the system's, "en" or "de"
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    // Discord: clear the status after this many minutes paused (0 never), no
    // time left, what the member list shows (app, artist, title), the button
    Q_PROPERTY(int discordPauseMinutes READ discordPauseMinutes WRITE setDiscordPauseMinutes NOTIFY changed)
    Q_PROPERTY(bool discordHideTimeLeft READ discordHideTimeLeft WRITE setDiscordHideTimeLeft NOTIFY changed)
    Q_PROPERTY(QString discordShows READ discordShows WRITE setDiscordShows NOTIFY changed)
    Q_PROPERTY(bool discordButton READ discordButton WRITE setDiscordButton NOTIFY changed)
    // sponsorblock: skip non-music parts of music videos
    Q_PROPERTY(bool sponsorBlock READ sponsorBlock WRITE setSponsorBlock NOTIFY changed)
    // which kinds of segments: music_offtopic, sponsor, selfpromo, interaction, intro, outro
    Q_PROPERTY(QStringList sponsorBlockCategories READ sponsorBlockCategories WRITE setSponsorBlockCategories
                   NOTIFY changed)
    Q_PROPERTY(bool compactSidebar READ compactSidebar WRITE setCompactSidebar NOTIFY changed)
    // the section shown at start and after signing in: home, explore, library, liked
    Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
    // the last queue comes back at start
    Q_PROPERTY(bool resumeSession READ resumeSession WRITE setResumeSession NOTIFY changed)
    // the window's last size, and the mini player's width
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY changed)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY changed)
    Q_PROPERTY(int miniWidth READ miniWidth WRITE setMiniWidth NOTIFY changed)
    Q_PROPERTY(bool clock READ clock WRITE setClock NOTIFY changed)
    Q_PROPERTY(bool clockSeconds READ clockSeconds WRITE setClockSeconds NOTIFY changed)
    // "" as the system, "24" or "12" hours
    Q_PROPERTY(QString clockHours READ clockHours WRITE setClockHours NOTIFY changed)
    // a see-through window background (the compositor may blur behind it); needs a restart
    Q_PROPERTY(bool transparentWindow READ transparentWindow WRITE setTransparentWindow NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    double volume() const;
    bool exponentialVolume() const;
    double volumeStep() const;
    QString audioDevice() const;
    bool normalizeLoudness() const;
    bool albumColors() const;
    int glowStrength() const;
    bool coloredProgress() const;
    int ambientStrength() const;
    int ambientSize() const;
    int windowOpacity() const;
    bool notifications() const;
    bool notifyOnResume() const;
    QString notificationUrgency() const;
    bool trayClickPlays() const;
    bool tray() const;
    bool closeToTray() const;
    bool discord() const;
    QString discordAppId() const;
    bool lyricsLrclib() const;
    bool globalShortcuts() const;
    bool skipDisliked() const;
    bool startPaused() const;
    bool videoMode() const;
    int videoHeight() const;
    QString captionLanguage() const;
    QString visualizer() const;
    QString downloadFormat() const;
    QString downloadFolder() const;
    int downloadMp3Kbps() const;
    bool downloadSkipExisting() const;
    int downloadPlaylistMax() const;
    bool lyricsGenius() const;
    bool lyricsMegalobiz() const;
    QString lyricsPreferred() const;
    bool lyricsInexact() const;
    QString lyricsGapText() const;
    bool lyricsRomanize() const;
    QString lyricsChinese() const;
    QString lyricsEffect() const;
    bool lyricsTimeCodes() const;
    QString language() const;
    int discordPauseMinutes() const;
    bool discordHideTimeLeft() const;
    QString discordShows() const;
    bool discordButton() const;
    bool sponsorBlock() const;
    QStringList sponsorBlockCategories() const;
    bool compactSidebar() const;
    QString startPage() const;
    bool resumeSession() const;
    int windowWidth() const;
    int windowHeight() const;
    int miniWidth() const;
    bool clock() const;
    bool clockSeconds() const;
    QString clockHours() const;
    bool transparentWindow() const;

    void setVolume(double volume);
    void setExponentialVolume(bool on);
    void setVolumeStep(double step);
    void setAudioDevice(const QString &device);
    void setNormalizeLoudness(bool on);
    void setAlbumColors(bool on);
    void setGlowStrength(int percent);
    void setColoredProgress(bool on);
    void setAmbientStrength(int percent);
    void setAmbientSize(int percent);
    void setWindowOpacity(int percent);
    void setNotifications(bool on);
    void setNotifyOnResume(bool on);
    void setNotificationUrgency(const QString &urgency);
    void setTrayClickPlays(bool on);
    void setTray(bool on);
    void setCloseToTray(bool on);
    void setDiscord(bool on);
    void setDiscordAppId(const QString &id);
    void setLyricsLrclib(bool on);
    void setGlobalShortcuts(bool on);
    void setSkipDisliked(bool on);
    void setStartPaused(bool on);
    void setVideoMode(bool on);
    void setVideoHeight(int height);
    void setCaptionLanguage(const QString &language);
    void setVisualizer(const QString &style);
    void setDownloadFormat(const QString &format);
    void setDownloadFolder(const QString &folder);
    void setDownloadMp3Kbps(int kbps);
    void setDownloadSkipExisting(bool on);
    void setDownloadPlaylistMax(int tracks);
    void setLyricsGenius(bool on);
    void setLyricsMegalobiz(bool on);
    void setLyricsPreferred(const QString &source);
    void setLyricsInexact(bool on);
    void setLyricsGapText(const QString &text);
    void setLyricsRomanize(bool on);
    void setLyricsChinese(const QString &to);
    void setLyricsEffect(const QString &effect);
    void setLyricsTimeCodes(bool on);
    void setLanguage(const QString &code);
    void setDiscordPauseMinutes(int minutes);
    void setDiscordHideTimeLeft(bool on);
    void setDiscordShows(const QString &shows);
    void setDiscordButton(bool on);
    void setSponsorBlock(bool on);
    void setSponsorBlockCategories(const QStringList &categories);
    void setCompactSidebar(bool on);
    void setStartPage(const QString &page);
    void setResumeSession(bool on);
    void setWindowWidth(int width);
    void setWindowHeight(int height);
    void setMiniWidth(int width);
    void setClock(bool on);
    void setClockSeconds(bool on);
    void setClockHours(const QString &hours);
    void setTransparentWindow(bool on);

    // the saved window size, read once at start (a call, so no binding follows it)
    Q_INVOKABLE QSize savedWindowSize() const { return {windowWidth(), windowHeight()}; }
    Q_INVOKABLE int savedMiniWidth() const { return miniWidth(); }

    // mpv volume (0..100) for a slider position and back
    Q_INVOKABLE double toPlayerVolume(double slider) const;
    Q_INVOKABLE double toSlider(double playerVolume) const;

    // for parts that keep their own keys (AudioEffects)
    QVariant value(const QString &key, const QVariant &fallback = {}) const { return m_store.value(key, fallback); }
    void set(const QString &key, const QVariant &value);

signals:
    void changed();

private:

    QSettings m_store;
};
