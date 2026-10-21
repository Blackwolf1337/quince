#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QtQml/qqmlregistration.h>

// User settings, kept in quince.conf in the config folder.
class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Settings are owned by App")

    // the slider position, 0..100; with exponentialVolume the heard volume is (slider/100)^3
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool exponentialVolume READ exponentialVolume WRITE setExponentialVolume NOTIFY changed)
    // slider steps of the mouse wheel and the volume keys, in percent
    Q_PROPERTY(double volumeStep READ volumeStep WRITE setVolumeStep NOTIFY changed)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY changed)
    Q_PROPERTY(bool normalizeLoudness READ normalizeLoudness WRITE setNormalizeLoudness NOTIFY changed)
    Q_PROPERTY(bool albumColors READ albumColors WRITE setAlbumColors NOTIFY changed)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
    Q_PROPERTY(bool tray READ tray WRITE setTray NOTIFY changed)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY changed)
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
    Q_PROPERTY(bool compactSidebar READ compactSidebar WRITE setCompactSidebar NOTIFY changed)
    Q_PROPERTY(bool clock READ clock WRITE setClock NOTIFY changed)
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
    bool notifications() const;
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
    bool compactSidebar() const;
    bool clock() const;
    bool transparentWindow() const;

    void setVolume(double volume);
    void setExponentialVolume(bool on);
    void setVolumeStep(double step);
    void setAudioDevice(const QString &device);
    void setNormalizeLoudness(bool on);
    void setAlbumColors(bool on);
    void setNotifications(bool on);
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
    void setCompactSidebar(bool on);
    void setClock(bool on);
    void setTransparentWindow(bool on);

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
