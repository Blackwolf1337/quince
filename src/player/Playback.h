#pragma once

#include "player/StreamResolver.h"

#include <QHash>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

class InnerTube;
class MpvPlayer;
class Queue;
class StreamResolver;

// What plays and what comes next. Queues come from the next endpoint the way
// the site gets them: the playlist id of a clicked track decides whether the
// queue is its album, its playlist or a radio. The stream of the next track
// is resolved while the current one plays and handed to mpv for a gapless
// change.
class Playback : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Playback is owned by App")
    Q_MOC_INCLUDE("player/Queue.h")

    Q_PROPERTY(Queue *queue READ queue CONSTANT)
    Q_PROPERTY(QVariantMap track READ track NOTIFY trackChanged)
    Q_PROPERTY(QString videoId READ videoId NOTIFY trackChanged)
    Q_PROPERTY(QString streamInfo READ streamInfo NOTIFY streamInfoChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY queueStateChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY queueStateChanged)
    // the stream playing has a picture (video mode found a video for it)
    Q_PROPERTY(bool videoPlaying READ videoPlaying NOTIFY streamInfoChanged)
    // [{name, language, generated}] of the video playing, and the one shown (-1: none)
    Q_PROPERTY(QVariantList captions READ captions NOTIFY captionsChanged)
    Q_PROPERTY(int caption READ caption WRITE setCaption NOTIFY captionsChanged)

public:
    Playback(MpvPlayer *player, StreamResolver *resolver, InnerTube *innerTube, QObject *parent = nullptr);
    ~Playback() override;

    Queue *queue() const { return m_queue; }
    QVariantMap track() const { return m_track; }
    QString videoId() const;
    QString streamInfo() const { return m_streamInfo; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool hasNext() const;
    bool hasPrevious() const;
    bool videoPlaying() const { return m_videoPlaying; }
    QVariantList captions() const { return m_captions; }
    int caption() const { return m_caption; }
    void setCaption(int index);

    // A song, video or episode plays at once and its queue follows; an
    // album, playlist or station plays its queue from the start.
    Q_INVOKABLE void play(const QVariantMap &item);
    Q_INVOKABLE void playPlaylist(const QString &playlistId, const QString &videoId, bool shuffle);
    Q_INVOKABLE bool playText(const QString &text);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void playNext(const QVariantMap &item);
    Q_INVOKABLE void addToQueue(const QVariantMap &item);
    Q_INVOKABLE void startRadio(const QVariantMap &item);

    // after a rating went through: the track and its queue entries show it
    void setLikeStatus(const QString &videoId, const QString &status);

    // turns loud tracks down to YouTube's reference level, like the site
    void setNormalizeLoudness(bool on);
    // skip-disliked-songs: going on (by itself or Next) passes over disliked tracks
    void setSkipDisliked(bool on) { m_skipDisliked = on; }
    // disable-autoplay: a track or list the user picks starts paused
    void setStartPaused(bool on) { m_startPaused = on; }
    // crossfade: seconds the next track overlaps the end of this one while
    // this one fades out, and how fast the next fades in (0: as long);
    // 0 plays them gapless (also in video mode)
    void setCrossfade(double seconds, double inSeconds);
    // video-toggle: videos play with their picture, songs with their music
    // video when YouTube has one; the track playing restarts in the new mode
    void setVideoMode(bool on);
    void setVideoHeight(int height);
    // caption language picked by itself in video mode ("" none)
    void setCaptionLanguage(const QString &language) { m_captionLanguage = language; }
    // where a play of videoId is reported to the account's history; empty
    // until the player answer for it arrived
    QString trackingUrl(const QString &videoId) const { return m_tracking.value(videoId); }

    // last queue and position, restored paused on the next start; the owner
    // saves before the player goes away
    void restoreSession();
    void saveSession() const;

signals:
    void trackChanged();
    void streamInfoChanged();
    void loadingChanged();
    void errorChanged();
    void queueStateChanged();
    void captionsChanged();
    // a stream began (its video id; in video mode a song's may be its video's)
    void streamStarted(const QString &videoId);

private:
    enum class QueueMode {
        Around, // the clicked track plays already, the queue is built around it
        Start, // replace the queue and play from its selected item
        Append, // automix: more of the same at the end
        InsertNext, // "Play next" for an album or playlist
        AddEnd, // "Add to queue" for an album or playlist
    };

    QJsonObject queueBody() const;
    void requestQueue(const QJsonObject &body, QueueMode mode);
    void applyQueue(const QVariantList &items, int selected, QueueMode mode);
    void loadMoreQueue();
    void startCurrent(double start = 0, bool paused = false);
    void goTo(int index);
    // the queue entry that plays after the current one; count() when none
    int nextIndex() const;
    // the one before it; -1 when none
    int previousIndex() const;
    void prefetchUpcoming();
    void onResolved(const Stream &stream);
    void onFailed(const QString &videoId, const QString &error);
    void onTrackStarted(const QString &tag);
    void onTrackEnded(const QString &tag, bool eof, const QString &error);
    void setTrack(const QVariantMap &item);
    void setStreamInfo(const QString &info);
    void setLoading(bool loading);
    void setError(const QString &error);
    void scheduleSave();
    void rememberPlayerInfo(const Stream &stream, const QString &queueId);
    // what plays for a queue entry: its own stream, or in video mode a song's
    // music video; known is false while that is being looked up
    struct Source
    {
        QString id;
        bool video = false;
        bool known = true;
    };
    Source sourceFor(const QVariantMap &item) const;
    void lookupCounterpart(const QVariantMap &item, std::function<void()> done);
    void applyCaptions(const Stream &stream);
    double crossfadeSeconds() const { return m_videoMode ? 0 : m_crossfade; }
    void checkCrossfade();
    double gainFor(const QString &videoId) const;

    MpvPlayer *m_player = nullptr;
    StreamResolver *m_resolver = nullptr;
    InnerTube *m_innerTube = nullptr;
    Queue *m_queue = nullptr;

    QVariantMap m_track;
    QString m_streamInfo;
    bool m_loading = false;
    QString m_error;

    QString m_wantedId; // the current item, waiting for its stream
    double m_wantedStart = 0;
    bool m_wantedPaused = false;
    QString m_loadedId; // playing in mpv
    QString m_queuedId; // in mpv after it, for a gapless change
    QString m_queuedInfo;
    // its stream, for a crossfade (gapless hands it to mpv at once)
    std::optional<Stream> m_upcoming;
    double m_crossfade = 0;
    double m_crossfadeIn = 0;
    // streams asked for the current and the next entry
    QString m_wantedStream;
    bool m_wantedVideo = false;
    QString m_loadedStream;
    QString m_upcomingStream;
    bool m_upcomingVideo = false;
    QString m_upcomingQueueId;

    bool m_videoMode = false;
    int m_videoHeight = 1080;
    bool m_videoPlaying = false;
    // song id to its music video id ("" when it has none)
    QHash<QString, QString> m_counterparts;
    QSet<QString> m_lookingUp;
    QVariantList m_captions; // with url, for setCaption
    int m_caption = -1;
    QString m_captionLanguage;
    bool m_captionPending = false;
    int m_failures = 0;
    // a stream URL that fails is resolved again once before the track is skipped
    QSet<QString> m_retried;
    QElapsedTimer m_startTimer; // since the current track was asked for
    QElapsedTimer m_playingFor;

    int m_queueGeneration = 0;
    bool m_queueLoading = false;
    QJsonObject m_queueRequest; // the last next request, radio continuations repeat it
    QString m_continuation;
    QString m_automixPlaylistId;
    QString m_automixParams;
    // album and cover for the items of an album queue, which come without them
    QVariantMap m_albumContext;

    QTimer m_saveTimer;

    // from the player endpoint, per video
    bool m_normalize = true;
    bool m_skipDisliked = false;
    bool m_startPaused = false;
    QHash<QString, double> m_loudness;
    QHash<QString, QString> m_tracking;
};
