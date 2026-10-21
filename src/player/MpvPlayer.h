#pragma once

#include "player/StreamResolver.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <optional>

struct mpv_handle;
struct mpv_event;

// Audio playback through libmpv. All calls into mpv are asynchronous; mpv's
// wakeup callback queues processEvents() onto the Qt event loop, so nothing
// here waits on mpv's threads.
//
// There are two mpv instances ("decks") for crossfades: the next track starts
// on the idle deck and becomes the active one while the old one fades out.
// Everything the UI sees comes from the active deck.
class MpvPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The player is owned by App")

    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    // [{name, description}] as mpv lists them; "auto" is the system default
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)

public:
    // audioOutput is mpv's --ao value; empty lets mpv choose ("null" for silent runs)
    explicit MpvPlayer(const QString &audioOutput, QObject *parent = nullptr);
    ~MpvPlayer() override;

    bool isValid() const { return m_decks[0].mpv != nullptr; }

    // Replaces what plays. tag names the entry in trackStarted and trackEnded;
    // gainDb applies to this file only (loudness normalization).
    void load(const Stream &stream, const QString &tag, double start = 0, bool paused = false, double gainDb = 0);
    // Plays stream right after the current file without a gap, replacing a
    // stream queued before.
    void queueNext(const Stream &stream, const QString &tag, double gainDb = 0);
    void clearQueued();
    // Starts stream on the other deck and fades over to it within seconds;
    // trackStarted(tag) follows once it plays, crossfadeFailed(tag) if it cannot.
    void crossfadeTo(const Stream &stream, const QString &tag, double gainDb, double seconds);
    bool crossfading() const { return m_fadeFrom >= 0 || !m_crossfadeTag.isEmpty(); }
    // gain of the file playing now, for when its loudness arrived late
    void setGain(double gainDb);

    // mpv's audio filter chain (af), set on both decks
    void setFilters(const QString &filters);
    // An audio visualization (lavfi graph from audio to [vo], e.g. showcqt)
    // drawn as video while no real video plays; empty for none.
    void setVisualizer(const QString &graph);
    // captions of the video playing: a WebVTT URL, or empty for none
    void setSubtitle(const QUrl &url);

    // the mpv instance a video item draws from: the active deck
    mpv_handle *renderHandle() const { return m_decks[m_active].mpv; }
    // A video item draws (its render context exists): only then is the
    // picture decoded; without one mpv would fail to open its output.
    void setVideoOutput(bool on);
    // playback speed, pitch kept; factor on top of it while skipping a silence
    void setSpeed(double speed);
    void setSpeedBoost(double factor);

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    bool idle() const { return m_idle; }
    bool buffering() const { return m_buffering; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QVariantList audioDevices() const { return m_audioDevices; }
    // Starts listing the outputs (audioDevices). Not done at start: mpv asks
    // every audio backend, which blocks its core, and so playback, until all
    // have answered.
    Q_INVOKABLE void watchAudioDevices();
    QString audioDevice() const { return m_audioDevice; }

    void setPaused(bool paused);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setAudioDevice(const QString &device);

    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stop();

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void idleChanged();
    void bufferingChanged();
    void volumeChanged();
    void mutedChanged();
    void audioDevicesChanged();
    void audioDeviceChanged();

    void trackStarted(const QString &tag);
    // eof: played to its end; error: why it stopped, empty if it did not fail
    void trackEnded(const QString &tag, bool eof, const QString &error);
    void playbackError(const QString &message);
    // audio runs (after a load or a seek)
    void playbackRestarted();
    void crossfadeFailed(const QString &tag);
    // the other deck plays now (after a crossfade); video items follow it
    void activeDeckChanged();
    // a video item draws now, but the video playing was opened without its
    // picture (mpv does not add it to an open file); load it again
    void pictureNeedsReload();
    // metadata of the "silence" filter (lavfi silencedetect) of the active deck
    void silenceMetadata(const QVariantMap &values);

private:
    struct Next
    {
        Stream stream;
        QString tag;
        double gainDb = 0;
    };
    struct Deck
    {
        mpv_handle *mpv = nullptr;
        // loadfile replies carry the playlist entry id that START_FILE and
        // END_FILE name later
        QHash<quint64, QString> replyTags;
        QHash<qint64, QString> entryTags;
        qint64 unmatchedStart = -1;
        // Tag of a file loaded with "replace" that has not started yet. Until
        // it has, mpv does not count it as playing, and playlist-clear would
        // remove it; the next track waits in waitingNext instead.
        QString startingTag;
        std::optional<Next> waitingNext;
    };

    static void wakeup(void *ctx);
    bool createDeck(int index);
    void processEvents();
    void handleEvent(int deck, const mpv_event &event);
    void command(int deck, const QVariant &args, quint64 reply = 0);
    void setDeckProperty(int deck, const char *name, const QVariant &value);
    // on every deck that exists
    void setMpvProperty(const char *name, const QVariant &value);
    void loadFile(int deck, const Stream &stream, const QString &tag, const char *flags, double start, double gainDb);
    void onStarted(int deck, const QString &tag);
    void beginFade();
    void stepFade();
    void endFade();
    void abortCrossfade();
    void applyVolume();
    void filterAudioDevices();

    Deck m_decks[2];
    int m_active = 0;
    QString m_audioOutput;
    std::atomic_bool m_wakePending{false};

    // crossfade: the deck fading out (-1: none), the tag starting on the
    // other deck, the curve's clock
    int m_fadeFrom = -1;
    QString m_crossfadeTag;
    double m_fadeSeconds = 0;
    double m_fadeProgress = 0;
    QElapsedTimer m_fadeClock;
    QTimer m_fadeTimer;

    double m_position = 0;
    double m_reportedPosition = 0;
    double m_duration = 0;
    bool m_paused = false;
    bool m_idle = true;
    bool m_buffering = false;
    double m_volume = 100;
    bool m_muted = false;
    double m_speed = 1.0;
    double m_speedBoost = 1.0;
    QString m_filters;
    QString m_visualizer;
    bool m_videoOutput = false;
    // the file playing on the active deck has a picture; per tag, of the
    // files handed to mpv
    bool m_activeIsVideo = false;
    QHash<QString, bool> m_tagIsVideo;
    // the file playing was opened with its picture on
    bool m_activePicture = false;
    QHash<QString, bool> m_tagPicture;
    QString m_audioDeviceWanted;
    QVariantList m_allAudioDevices;
    QVariantList m_audioDevices; // of the audio backend in use
    bool m_watchingDevices = false;
    QString m_audioDevice;
    QString m_currentAo;

    quint64 m_nextReply = 1;
};
