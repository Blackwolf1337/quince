#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class MpvPlayer;
class Playback;
class Settings;
class UiHost;

// MPRIS on the session bus (org.mpris.MediaPlayer2.quince), so media keys
// bound to playerctl and desktop media widgets control Quince. Linux only.
class Mpris : public QObject
{
    Q_OBJECT

public:
    Mpris(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host, QObject *parent = nullptr);
    ~Mpris() override;

    Playback *playback() const { return m_playback; }
    MpvPlayer *player() const { return m_player; }
    Settings *settings() const { return m_settings; }

    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canSeek() const;

    void setVolume(double volume);
    void raise();

private:
    void propertiesChanged(const QString &interface, const QVariantMap &changed);
    void onTrackChanged();
    void onPositionChanged();

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;
    UiHost *m_host = nullptr;
    bool m_registered = false;
    double m_lastPosition = 0;
};

// org.mpris.MediaPlayer2
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRoot(Mpris *mpris);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Quince"); }
    QString desktopEntry() const { return QStringLiteral("quince"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("https")}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise();
    void Quit();

private:
    Mpris *m_mpris;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPlay)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayer(Mpris *mpris);

    QString playbackStatus() const { return m_mpris->playbackStatus(); }
    double rate() const { return 1.0; }
    void setRate(double) {}
    QVariantMap metadata() const { return m_mpris->metadata(); }
    double volume() const { return m_mpris->volume(); }
    void setVolume(double volume) { m_mpris->setVolume(volume); }
    qlonglong position() const { return m_mpris->position(); }
    bool canGoNext() const { return m_mpris->canGoNext(); }
    bool canGoPrevious() const { return m_mpris->canGoPrevious(); }
    bool canPlay() const { return m_mpris->canPlay(); }
    bool canSeek() const { return m_mpris->canSeek(); }
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qlonglong position);

private:
    Mpris *m_mpris;
};
