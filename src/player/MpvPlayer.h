#pragma once

#include "player/StreamResolver.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <optional>

#include <atomic>

struct mpv_handle;
struct mpv_event;

// Audio playback through libmpv. All calls into mpv are asynchronous; mpv's
// wakeup callback queues processEvents() onto the Qt event loop, so nothing
// here waits on mpv's threads.
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

    bool isValid() const { return m_mpv != nullptr; }

    // Replaces what plays. tag names the entry in trackStarted and trackEnded;
    // gainDb applies to this file only (loudness normalization).
    void load(const Stream &stream, const QString &tag, double start = 0, bool paused = false, double gainDb = 0);
    // Plays stream right after the current file without a gap, replacing a
    // stream queued before.
    void queueNext(const Stream &stream, const QString &tag, double gainDb = 0);
    void clearQueued();
    // gain of the file playing now, for when its loudness arrived late
    void setGain(double gainDb);

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

private:
    static void wakeup(void *ctx);
    void processEvents();
    void handleEvent(const mpv_event &event);
    void command(const QVariant &args, quint64 reply = 0);
    void loadFile(const Stream &stream, const QString &tag, const char *flags, double start, double gainDb);
    void onStarted(const QString &tag);
    void filterAudioDevices();
    void setMpvProperty(const char *name, const QVariant &value);

    mpv_handle *m_mpv = nullptr;
    std::atomic_bool m_wakePending{false};

    double m_position = 0;
    double m_reportedPosition = 0;
    double m_duration = 0;
    bool m_paused = false;
    bool m_idle = true;
    bool m_buffering = false;
    double m_volume = 100;
    bool m_muted = false;
    QVariantList m_allAudioDevices;
    QVariantList m_audioDevices; // of the audio backend in use
    bool m_watchingDevices = false;
    QString m_audioDevice;
    QString m_currentAo;

    // loadfile replies carry the playlist entry id that START_FILE and
    // END_FILE name later
    quint64 m_nextReply = 1;
    QHash<quint64, QString> m_replyTags;
    QHash<qint64, QString> m_entryTags;
    qint64 m_unmatchedStart = -1;
    // Tag of a file loaded with "replace" that has not started yet. Until it
    // has, mpv does not count it as playing, and playlist-clear would remove
    // it; the next track waits in m_waitingNext instead.
    QString m_startingTag;
    struct Next
    {
        Stream stream;
        QString tag;
        double gainDb = 0;
    };
    std::optional<Next> m_waitingNext;
};
