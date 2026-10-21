#include "player/Playback.h"
#include "player/MpvPlayer.h"
#include "player/Queue.h"
#include "player/StreamResolver.h"
#include "ytm/InnerTube.h"
#include "ytm/Json.h"
#include "ytm/Parser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcPlayback, "quince.playback")

namespace {

const QString kVideoId = QStringLiteral("videoId");
// the site's params for a shuffled playlist queue
const QString kShuffleParams = QStringLiteral("wAEB8gECKAE%3D");
// a queue with this few items left fetches more
constexpr int kQueueLowWater = 3;
// unplayable tracks skipped in a row before giving up
constexpr int kMaxFailures = 3;
// "previous" goes back a track within this many seconds, else to the start
constexpr double kRestartThreshold = 3.0;
constexpr int kSaveDelayMs = 2000;
// player answers kept before the oldest are dropped
constexpr int kMaxPlayerInfo = 400;

QString sessionPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/session.json");
}

QString browseToPlaylistId(const QString &id)
{
    return id.startsWith(u"VL") ? id.mid(2) : id;
}

// the page item knows the square cover and the album; the queue item may
// only know the video
QVariantMap mergeItem(QVariantMap queueItem, const QVariantMap &pageItem)
{
    for (auto it = pageItem.cbegin(); it != pageItem.cend(); ++it) {
        if (it.value().isValid() && !(it.value().typeId() == QMetaType::QString && it.value().toString().isEmpty()))
            queueItem.insert(it.key(), it.value());
    }
    return queueItem;
}

// album playlists (OLAK5uy_...) hold exactly one album
bool isAlbumPlaylist(const QString &playlistId)
{
    return playlistId.startsWith(u"OLAK5uy_");
}

QVariantList withAlbum(QVariantList items, const QVariantMap &context)
{
    if (context.isEmpty())
        return items;
    for (QVariant &v : items) {
        QVariantMap item = v.toMap();
        if (!item.contains(QStringLiteral("album")))
            item.insert(QStringLiteral("album"), context.value(QStringLiteral("album")));
        item.insert(QStringLiteral("thumbnail"), context.value(QStringLiteral("thumbnail")));
        v = item;
    }
    return items;
}

QString streamInfoOf(const Stream &stream)
{
    const QString audio = QStringLiteral("%1 %2 kbit/s").arg(stream.codec).arg(qRound(stream.bitrate));
    return stream.video ? QStringLiteral("%1p • %2").arg(stream.height).arg(audio) : audio;
}

} // namespace

Playback::Playback(MpvPlayer *player, StreamResolver *resolver, InnerTube *innerTube, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_resolver(resolver)
    , m_innerTube(innerTube)
    , m_queue(new Queue(this))
{
    connect(m_resolver, &StreamResolver::resolved, this, &Playback::onResolved);
    connect(m_resolver, &StreamResolver::failed, this, &Playback::onFailed);
    connect(m_player, &MpvPlayer::trackStarted, this, &Playback::onTrackStarted);
    connect(m_player, &MpvPlayer::trackEnded, this, &Playback::onTrackEnded);
    connect(m_player, &MpvPlayer::positionChanged, this, &Playback::checkCrossfade);
    connect(m_player, &MpvPlayer::pictureNeedsReload, this, [this] {
        if (!m_loadedId.isEmpty() && m_loadedId == m_queue->videoIdAt(m_queue->current()))
            startCurrent(m_player->position(), m_player->paused());
    });
    connect(m_player, &MpvPlayer::crossfadeFailed, this, [this](const QString &tag) {
        if (tag != m_queuedId)
            return;
        // the track then follows the usual way when this one ends
        m_queuedId.clear();
        m_upcoming.reset();
        m_resolver->forget(tag);
    });
    connect(m_player, &MpvPlayer::playbackRestarted, this, [this] {
        if (m_captionPending) {
            m_captionPending = false;
            setCaption(m_caption);
        }
        if (m_startTimer.isValid()) {
            qCDebug(lcPlayback) << "sound after" << m_startTimer.elapsed() << "ms";
            m_startTimer.invalidate();
        }
    });
    connect(m_queue, &Queue::upcomingChanged, this, &Playback::prefetchUpcoming);
    connect(m_queue, &Queue::currentChanged, this, &Playback::queueStateChanged);
    connect(m_queue, &Queue::countChanged, this, &Playback::queueStateChanged);
    connect(m_queue, &Queue::countChanged, this, &Playback::scheduleSave);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &Playback::saveSession);
}

Playback::~Playback() = default;

QString Playback::videoId() const
{
    return m_track.value(kVideoId).toString();
}

bool Playback::hasNext() const
{
    return nextIndex() < m_queue->count();
}

int Playback::nextIndex() const
{
    int index = m_queue->current() + 1;
    if (m_skipDisliked) {
        while (index < m_queue->count()
               && m_queue->at(index).value(QStringLiteral("likeStatus")).toString() == u"DISLIKE")
            ++index;
    }
    return index;
}

bool Playback::hasPrevious() const
{
    return previousIndex() >= 0;
}

int Playback::previousIndex() const
{
    int index = m_queue->current() - 1;
    if (m_skipDisliked) {
        while (index >= 0 && m_queue->at(index).value(QStringLiteral("likeStatus")).toString() == u"DISLIKE")
            --index;
    }
    return index;
}

void Playback::setTrack(const QVariantMap &item)
{
    m_track = item;
    emit trackChanged();
    scheduleSave();
}

void Playback::setStreamInfo(const QString &info)
{
    if (m_streamInfo == info)
        return;
    m_streamInfo = info;
    emit streamInfoChanged();
}

void Playback::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void Playback::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

QJsonObject Playback::queueBody() const
{
    return {
        {QStringLiteral("enablePersistentPlaylistPanel"), true},
        {QStringLiteral("isAudioOnly"), true},
        {QStringLiteral("tunerSettingValue"), QStringLiteral("AUTOMIX_SETTING_NORMAL")},
    };
}

void Playback::play(const QVariantMap &item)
{
    const QString videoId = item.value(kVideoId).toString();
    if (!videoId.isEmpty()) {
        m_albumContext.clear();
        if (isAlbumPlaylist(item.value(QStringLiteral("playlistId")).toString())
            && item.contains(QStringLiteral("album")) && item.contains(QStringLiteral("thumbnail"))) {
            m_albumContext = {
                {QStringLiteral("album"), item.value(QStringLiteral("album"))},
                {QStringLiteral("thumbnail"), item.value(QStringLiteral("thumbnail"))},
            };
        }
        m_queue->setItems({item}, 0);
        startCurrent(0, m_startPaused);

        QJsonObject body = queueBody();
        body.insert(QStringLiteral("videoId"), videoId);
        QString playlistId = item.value(QStringLiteral("playlistId")).toString();
        if (playlistId.isEmpty())
            playlistId = QStringLiteral("RDAMVM") + videoId;
        body.insert(QStringLiteral("playlistId"), browseToPlaylistId(playlistId));
        const QString params = item.value(QStringLiteral("params")).toString();
        if (!params.isEmpty() && item.value(QStringLiteral("kind")).toString() != u"page")
            body.insert(QStringLiteral("params"), params);
        const QString videoType = item.value(QStringLiteral("videoType")).toString();
        body.insert(QStringLiteral("watchEndpointMusicSupportedConfigs"),
                    QJsonObject{{QStringLiteral("watchEndpointMusicConfig"),
                                 QJsonObject{{QStringLiteral("hasPersistentPlaylistPanel"), true},
                                             {QStringLiteral("musicVideoType"),
                                              videoType.isEmpty() ? QStringLiteral("MUSIC_VIDEO_TYPE_ATV")
                                                                  : videoType}}}});
        requestQueue(body, QueueMode::Around);
        return;
    }

    QString playlistId = item.value(QStringLiteral("playPlaylistId")).toString();
    if (playlistId.isEmpty())
        playlistId = item.value(QStringLiteral("playlistId")).toString();
    const QString firstVideo = item.value(QStringLiteral("playVideoId")).toString();
    if (playlistId.isEmpty() && firstVideo.isEmpty())
        return;
    playPlaylist(playlistId, firstVideo, false);
    // an album page header or card: its tracks get its name and cover
    if (item.value(QStringLiteral("kind")).toString() == u"album" && isAlbumPlaylist(playlistId)) {
        m_albumContext = {
            {QStringLiteral("album"), QVariantMap{{QStringLiteral("name"), item.value(QStringLiteral("title"))},
                                                  {QStringLiteral("id"), item.value(QStringLiteral("browseId"))}}},
            {QStringLiteral("thumbnail"), item.value(QStringLiteral("thumbnail"))},
        };
    }
}

void Playback::playPlaylist(const QString &playlistId, const QString &videoId, bool shuffle)
{
    QJsonObject body = queueBody();
    if (!playlistId.isEmpty())
        body.insert(QStringLiteral("playlistId"), browseToPlaylistId(playlistId));
    if (!videoId.isEmpty())
        body.insert(QStringLiteral("videoId"), videoId);
    if (shuffle)
        body.insert(QStringLiteral("params"), kShuffleParams);
    m_albumContext.clear();
    setError({});
    setLoading(true);
    requestQueue(body, QueueMode::Start);
}

bool Playback::playText(const QString &text)
{
    const QString videoId = StreamResolver::extractVideoId(text);
    if (videoId.isEmpty()) {
        setError(tr("That is not a YouTube link or video id."));
        return false;
    }
    play({{kVideoId, videoId}});
    return true;
}

void Playback::startRadio(const QVariantMap &item)
{
    QString playlistId = item.value(QStringLiteral("radioPlaylistId")).toString();
    const QString videoId = item.value(kVideoId).toString();
    if (playlistId.isEmpty() && !videoId.isEmpty())
        playlistId = QStringLiteral("RDAMVM") + videoId;
    if (!playlistId.isEmpty())
        playPlaylist(playlistId, videoId, false);
}

void Playback::setNormalizeLoudness(bool on)
{
    if (m_normalize == on)
        return;
    m_normalize = on;
    if (!m_loadedId.isEmpty())
        m_player->setGain(gainFor(m_loadedId));
}

// YouTube turns loud tracks down to its reference level and never up
double Playback::gainFor(const QString &videoId) const
{
    if (!m_normalize)
        return 0;
    return -qMax(0.0, m_loudness.value(videoId, 0.0));
}

// kept under the queue entry's id, which may differ from the stream's (a song's video)
void Playback::rememberPlayerInfo(const Stream &stream, const QString &queueId)
{
    if (m_loudness.size() + m_tracking.size() > kMaxPlayerInfo) {
        m_loudness.clear();
        m_tracking.clear();
    }
    if (!std::isnan(stream.loudnessDb))
        m_loudness.insert(queueId, stream.loudnessDb);
    if (!stream.trackingUrl.isEmpty())
        m_tracking.insert(queueId, stream.trackingUrl);
}

Playback::Source Playback::sourceFor(const QVariantMap &item) const
{
    const QString id = item.value(kVideoId).toString();
    if (!m_videoMode)
        return {id, false, true};
    const QString type = item.value(QStringLiteral("videoType")).toString();
    // podcasts stay sound only; music videos and uploads are videos themselves
    if (type == u"MUSIC_VIDEO_TYPE_PODCAST_EPISODE")
        return {id, false, true};
    if (!type.isEmpty() && type != u"MUSIC_VIDEO_TYPE_ATV")
        return {id, true, true};
    // a bare link (--play, pasted): of unknown kind, so it shows its own picture
    if (type.isEmpty() && item.value(QStringLiteral("title")).toString().isEmpty())
        return {id, true, true};
    const auto it = m_counterparts.constFind(id);
    if (it == m_counterparts.cend())
        return {id, false, false};
    return it->isEmpty() ? Source{id, false, true} : Source{*it, true, true};
}

// A song's music video: the first official video (OMV) by the same artist in
// a video search. The queue gives it only to some accounts, a search to all.
void Playback::lookupCounterpart(const QVariantMap &item, std::function<void()> done)
{
    const QString id = item.value(kVideoId).toString();
    if (m_lookingUp.contains(id))
        return;
    m_lookingUp.insert(id);
    QStringList artists;
    for (const QVariant &a : item.value(QStringLiteral("artists")).toList())
        artists.append(a.toMap().value(QStringLiteral("name")).toString());
    if (artists.isEmpty() && !item.value(QStringLiteral("artistText")).toString().isEmpty())
        artists.append(item.value(QStringLiteral("artistText")).toString());
    const QString title = item.value(QStringLiteral("title")).toString();
    const QJsonObject body{
        {QStringLiteral("query"), (artists.value(0) + u' ' + title).trimmed()},
        // the "Videos" filter of the search page
        {QStringLiteral("params"), QStringLiteral("EgWKAQIQAWoMEA4QChADEAQQCRAF")},
    };
    m_innerTube->post(QStringLiteral("search"), body, this,
                      [this, id, artists, done](const QJsonObject &response, const QString &error) {
        m_lookingUp.remove(id);
        QString found;
        if (error.isEmpty()) {
            const ytm::Page page = ytm::parseSearch(response);
            for (const QVariant &shelf : page.shelves) {
                for (const QVariant &v : shelf.toMap().value(QStringLiteral("items")).toList()) {
                    const QVariantMap result = v.toMap();
                    if (result.value(QStringLiteral("videoType")).toString() != u"MUSIC_VIDEO_TYPE_OMV")
                        continue;
                    const QString by = result.value(QStringLiteral("subtitle")).toString()
                                       + result.value(QStringLiteral("artistText")).toString();
                    if (!artists.isEmpty() && !by.contains(artists.first(), Qt::CaseInsensitive))
                        continue;
                    found = result.value(kVideoId).toString();
                    break;
                }
                if (!found.isEmpty())
                    break;
            }
        }
        qCDebug(lcPlayback) << "music video of" << id << "is" << (found.isEmpty() ? QStringLiteral("none") : found);
        // a failed search counts as no video; the song plays as sound
        m_counterparts.insert(id, found);
        done();
    });
}

void Playback::setLikeStatus(const QString &videoId, const QString &status)
{
    for (int i = m_queue->indexOf(videoId); i >= 0; i = m_queue->indexOf(videoId, i + 1)) {
        QVariantMap item = m_queue->at(i);
        item.insert(QStringLiteral("likeStatus"), status);
        m_queue->replace(i, item);
    }
    if (videoId == this->videoId()) {
        m_track.insert(QStringLiteral("likeStatus"), status);
        emit trackChanged();
    }
}

void Playback::playNext(const QVariantMap &item)
{
    if (!item.value(kVideoId).toString().isEmpty()) {
        m_queue->insert(m_queue->current() + 1, item);
        return;
    }
    const QString playlistId = item.value(QStringLiteral("playPlaylistId")).toString();
    if (playlistId.isEmpty())
        return;
    QJsonObject body = queueBody();
    body.insert(QStringLiteral("playlistId"), browseToPlaylistId(playlistId));
    requestQueue(body, QueueMode::InsertNext);
}

void Playback::addToQueue(const QVariantMap &item)
{
    if (!item.value(kVideoId).toString().isEmpty()) {
        if (m_queue->count() == 0)
            play(item);
        else
            m_queue->append({item});
        return;
    }
    const QString playlistId = item.value(QStringLiteral("playPlaylistId")).toString();
    if (playlistId.isEmpty())
        return;
    QJsonObject body = queueBody();
    body.insert(QStringLiteral("playlistId"), browseToPlaylistId(playlistId));
    requestQueue(body, QueueMode::AddEnd);
}

void Playback::requestQueue(const QJsonObject &body, QueueMode mode)
{
    // a new queue makes answers for the old one useless; additions do not
    const bool replaces = mode == QueueMode::Around || mode == QueueMode::Start;
    if (replaces) {
        ++m_queueGeneration;
        m_queueRequest = body;
        m_continuation.clear();
        m_automixPlaylistId.clear();
    }
    const int generation = m_queueGeneration;
    m_queueLoading = true;

    m_innerTube->post(QStringLiteral("next"), body, this,
                      [this, generation, mode, replaces, body](const QJsonObject &response, const QString &error) {
        if (generation != m_queueGeneration)
            return;
        m_queueLoading = false;
        if (!error.isEmpty()) {
            if (mode == QueueMode::Start) {
                setLoading(false);
                setError(error);
            }
            qCWarning(lcPlayback) << "queue request failed:" << error;
            return;
        }

        const ytm::WatchQueue queue = ytm::parseWatchNext(response);
        if (replaces || mode == QueueMode::Append) {
            m_queueRequest = body;
            m_continuation = queue.continuation;
            m_automixPlaylistId = queue.automixPlaylistId;
            m_automixParams = queue.automixParams;
        }
        applyQueue(queue.items, queue.selected, mode);
    });
}

void Playback::applyQueue(const QVariantList &items, int selected, QueueMode mode)
{
    switch (mode) {
    case QueueMode::Around: {
        const QVariantMap playing = m_queue->currentItem();
        const QString videoId = playing.value(kVideoId).toString();
        QVariantList list = withAlbum(items, m_albumContext);
        int at = -1;
        if (selected >= 0 && list.at(selected).toMap().value(kVideoId).toString() == videoId)
            at = selected;
        for (int i = 0; at < 0 && i < list.size(); ++i) {
            if (list.at(i).toMap().value(kVideoId).toString() == videoId)
                at = i;
        }
        if (at < 0) {
            list.prepend(playing);
            at = 0;
        } else {
            list[at] = mergeItem(list.at(at).toMap(), playing);
        }
        m_queue->setItems(list, at);
        if (m_track.value(kVideoId).toString() == videoId)
            setTrack(m_queue->currentItem());
        break;
    }
    case QueueMode::Start:
        if (items.isEmpty()) {
            setLoading(false);
            setError(tr("YouTube Music has nothing to play there."));
            return;
        }
        m_queue->setItems(withAlbum(items, m_albumContext), qMax(selected, 0));
        startCurrent(0, m_startPaused);
        break;
    case QueueMode::Append: {
        QSet<QString> known;
        for (const QVariant &v : m_queue->items())
            known.insert(v.toMap().value(kVideoId).toString());
        QVariantList fresh;
        for (const QVariant &v : items) {
            if (!known.contains(v.toMap().value(kVideoId).toString()))
                fresh.append(v);
        }
        m_queue->append(fresh);
        break;
    }
    case QueueMode::InsertNext: {
        int at = m_queue->current() + 1;
        for (const QVariant &v : items)
            m_queue->insert(at++, v.toMap());
        break;
    }
    case QueueMode::AddEnd:
        if (m_queue->count() == 0) {
            m_queue->setItems(items, 0);
            startCurrent();
        } else {
            m_queue->append(items);
        }
        break;
    }
}

void Playback::loadMoreQueue()
{
    if (m_queueLoading)
        return;

    if (!m_continuation.isEmpty()) {
        const int generation = m_queueGeneration;
        m_queueLoading = true;
        m_innerTube->continuation(QStringLiteral("next"), m_queueRequest, m_continuation, false, this,
                                  [this, generation](const QJsonObject &response, const QString &error) {
            if (generation != m_queueGeneration)
                return;
            m_queueLoading = false;
            if (!error.isEmpty()) {
                qCWarning(lcPlayback) << "queue continuation failed:" << error;
                return;
            }
            const ytm::WatchQueue queue = ytm::parseWatchContinuation(response);
            m_continuation = queue.continuation;
            applyQueue(queue.items, -1, QueueMode::Append);
        });
        return;
    }

    if (!m_automixPlaylistId.isEmpty()) {
        QJsonObject body = queueBody();
        body.insert(QStringLiteral("playlistId"), m_automixPlaylistId);
        if (!m_automixParams.isEmpty())
            body.insert(QStringLiteral("params"), m_automixParams);
        m_automixPlaylistId.clear();
        requestQueue(body, QueueMode::Append);
    }
}

void Playback::startCurrent(double start, bool paused)
{
    const QVariantMap item = m_queue->currentItem();
    if (item.isEmpty())
        return;
    setTrack(item);
    setError({});
    m_wantedId = item.value(kVideoId).toString();
    m_wantedStart = start;
    m_wantedPaused = paused;
    setLoading(true);
    m_startTimer.start();
    // A track queued behind the one playing would start by itself when the
    // replaced file stops, and move the queue on.
    if (!m_queuedId.isEmpty()) {
        m_player->clearQueued();
        m_queuedId.clear();
        m_upcoming.reset();
    }
    const Source source = sourceFor(item);
    if (!source.known) {
        const int index = m_queue->current();
        const QString id = m_wantedId;
        lookupCounterpart(item, [this, index, id, start, paused] {
            if (m_queue->current() == index && m_queue->videoIdAt(index) == id && m_wantedId == id)
                startCurrent(start, paused);
        });
        return;
    }
    m_wantedStream = source.id;
    m_wantedVideo = source.video;
    m_resolver->resolve(source.id, source.video);
}

void Playback::prefetchUpcoming()
{
    const int current = m_queue->current();
    if (current >= m_queue->count() - kQueueLowWater)
        loadMoreQueue();

    // only once the current track is in mpv, so the order stays right
    if (m_loadedId.isEmpty() || m_loadedId != m_queue->videoIdAt(current))
        return;

    const QVariantMap item = m_queue->at(nextIndex());
    const QString upcoming = item.value(kVideoId).toString();
    if (upcoming == m_queuedId)
        return;
    if (!m_queuedId.isEmpty()) {
        m_player->clearQueued();
        m_queuedId.clear();
        m_upcoming.reset();
    }
    m_upcomingQueueId = upcoming;
    if (upcoming.isEmpty())
        return;
    const Source source = sourceFor(item);
    if (!source.known) {
        lookupCounterpart(item, [this] { prefetchUpcoming(); });
        return;
    }
    m_upcomingStream = source.id;
    m_upcomingVideo = source.video;
    m_resolver->resolve(source.id, source.video);
}

void Playback::onResolved(const Stream &stream)
{
    if (stream.aacAsked)
        return;
    if (!m_wantedId.isEmpty() && stream.videoId == m_wantedStream && stream.videoAsked == m_wantedVideo) {
        const QString queueId = std::exchange(m_wantedId, QString());
        rememberPlayerInfo(stream, queueId);
        m_failures = 0;
        setLoading(false);
        qCDebug(lcPlayback) << "resolved after" << m_startTimer.elapsed() << "ms from" << stream.client;
        m_player->load(stream, queueId, m_wantedStart, m_wantedPaused, gainFor(queueId));
        m_loadedId = queueId;
        m_loadedStream = stream.videoId;
        m_playingFor.start();
        m_queuedId.clear();
        m_upcoming.reset();
        m_videoPlaying = stream.video;
        setStreamInfo(streamInfoOf(stream));
        applyCaptions(stream);
        emit streamStarted(stream.videoId);
        prefetchUpcoming();
        return;
    }

    const int current = m_queue->current();
    const QString nextQueueId = m_queue->videoIdAt(nextIndex());
    if (stream.videoId == m_upcomingStream && stream.videoAsked == m_upcomingVideo
        && nextQueueId == m_upcomingQueueId && m_loadedId == m_queue->videoIdAt(current)
        && m_queuedId != nextQueueId) {
        rememberPlayerInfo(stream, nextQueueId);
        // gapless: mpv appends it now; crossfade: checkCrossfade starts it
        if (crossfadeSeconds() <= 0)
            m_player->queueNext(stream, nextQueueId, gainFor(nextQueueId));
        m_queuedId = nextQueueId;
        m_queuedInfo = streamInfoOf(stream);
        m_upcoming = stream;
    }
}

void Playback::setVideoMode(bool on)
{
    if (on == m_videoMode)
        return;
    m_videoMode = on;
    // the queued stream was for the other mode
    if (!m_queuedId.isEmpty()) {
        m_player->clearQueued();
        m_queuedId.clear();
        m_upcoming.reset();
    }
    if (!m_loadedId.isEmpty() && m_loadedId == m_queue->videoIdAt(m_queue->current()))
        startCurrent(m_player->position(), m_player->paused());
}

void Playback::setVideoHeight(int height)
{
    if (height == m_videoHeight)
        return;
    m_videoHeight = height;
    m_resolver->setVideoHeight(height);
    if (m_videoPlaying && !m_loadedId.isEmpty())
        startCurrent(m_player->position(), m_player->paused());
}

void Playback::applyCaptions(const Stream &stream)
{
    m_captions = stream.captions;
    m_caption = -1;
    m_captionPending = false;
    if (!m_captionLanguage.isEmpty()) {
        // a written track of the language before a generated one
        for (int pass = 0; pass < 2 && m_caption < 0; ++pass) {
            for (int i = 0; i < m_captions.size(); ++i) {
                const QVariantMap c = m_captions.at(i).toMap();
                if (c.value(QStringLiteral("language")).toString().startsWith(m_captionLanguage)
                    && c.value(QStringLiteral("generated")).toBool() == (pass == 1)) {
                    m_caption = i;
                    break;
                }
            }
        }
        // mpv takes captions once the file is open
        m_captionPending = m_caption >= 0;
    }
    emit captionsChanged();
}

void Playback::setCaption(int index)
{
    m_caption = index >= 0 && index < m_captions.size() ? index : -1;
    m_player->setSubtitle(m_caption >= 0 ? m_captions.at(m_caption).toMap().value(QStringLiteral("url")).toUrl()
                                         : QUrl());
    emit captionsChanged();
}

void Playback::setCrossfade(double seconds, double inSeconds)
{
    const double old = m_crossfade;
    m_crossfade = seconds;
    m_crossfadeIn = inSeconds;
    if (!m_upcoming || m_player->crossfading() || m_videoMode)
        return;
    // the queued track moves between mpv's playlist and the crossfade
    if (old <= 0 && seconds > 0)
        m_player->clearQueued();
    else if (old > 0 && seconds <= 0)
        m_player->queueNext(*m_upcoming, m_queuedId, gainFor(m_queuedId));
}

// the next track starts on the other deck when this one is that close to its end
void Playback::checkCrossfade()
{
    if (crossfadeSeconds() <= 0 || !m_upcoming || m_queuedId.isEmpty() || m_player->crossfading()
        || m_player->paused())
        return;
    const double duration = m_player->duration();
    const double position = m_player->position();
    if (duration <= 0 || position <= 0)
        return;
    // short tracks fade over at most half of themselves
    const double seconds = qMin(m_crossfade, duration / 2);
    if (duration - position > seconds)
        return;
    qCDebug(lcPlayback) << "crossfade to" << m_queuedId << "over" << seconds << "s";
    const double in = m_crossfadeIn > 0 ? qMin(m_crossfadeIn, seconds) : seconds;
    m_player->crossfadeTo(*m_upcoming, m_queuedId, gainFor(m_queuedId), seconds, in);
}

void Playback::onFailed(const QString &videoId, const QString &error)
{
    if (m_wantedId.isEmpty() || videoId != m_wantedStream)
        return;
    m_wantedId.clear();
    setLoading(false);
    setError(tr("Could not play \"%1\": %2").arg(m_track.value(QStringLiteral("title")).toString(), error));
    if (++m_failures < kMaxFailures && hasNext())
        next();
}

void Playback::onTrackStarted(const QString &tag)
{
    if (tag.isEmpty() || tag != m_queuedId)
        return;
    // mpv went on to the queued stream by itself
    m_loadedId = tag;
    m_playingFor.start();
    m_queuedId.clear();
    if (m_upcoming) {
        m_loadedStream = m_upcoming->videoId;
        m_videoPlaying = m_upcoming->video;
        applyCaptions(*m_upcoming);
        emit streamStarted(m_loadedStream);
    }
    m_upcoming.reset();
    setStreamInfo(m_queuedInfo);
    const int next = nextIndex();
    if (m_queue->videoIdAt(next) == tag)
        m_queue->setCurrent(next);
    setTrack(m_queue->currentItem());
}

void Playback::onTrackEnded(const QString &tag, bool eof, const QString &error)
{
    if (tag.isEmpty() || tag != m_loadedId)
        return;
    if (!error.isEmpty()) {
        qCWarning(lcPlayback) << "stream of" << tag << "failed:" << error;
        m_loadedId.clear();
        m_queuedId.clear();
        m_upcoming.reset();
        // googlevideo refuses some URLs (403); a fresh one, from the other client, usually works
        m_resolver->forget(m_loadedStream);
        if (!m_retried.contains(tag) && m_queue->videoIdAt(m_queue->current()) == tag) {
            m_retried.insert(tag);
            // mid-track failures go on where they stopped
            const double resumeAt = m_playingFor.isValid() && m_playingFor.elapsed() > 5000 ? m_player->position() : 0;
            startCurrent(resumeAt, false);
            return;
        }
        setError(tr("Playback stopped: %1").arg(error));
        if (++m_failures < kMaxFailures && hasNext())
            next();
        return;
    }
    // with a queued stream mpv goes on by itself and trackStarted follows
    if (eof && m_queuedId.isEmpty()) {
        m_loadedId.clear();
        if (hasNext())
            next();
    }
}

void Playback::playIndex(int index)
{
    if (index < 0 || index >= m_queue->count())
        return;
    m_queue->setCurrent(index);
    startCurrent(0, m_startPaused);
}

void Playback::goTo(int index)
{
    if (index < 0 || index >= m_queue->count())
        return;
    m_queue->setCurrent(index);
    startCurrent();
}

void Playback::next()
{
    if (hasNext())
        goTo(nextIndex());
}

void Playback::previous()
{
    if (m_player->position() > kRestartThreshold || !hasPrevious()) {
        m_player->seek(0);
        return;
    }
    goTo(previousIndex());
}

void Playback::scheduleSave()
{
    m_saveTimer.start();
}

void Playback::saveSession() const
{
    const QString path = sessionPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject session{
        {QStringLiteral("items"), QJsonArray::fromVariantList(m_queue->items())},
        {QStringLiteral("current"), m_queue->current()},
        {QStringLiteral("position"), m_wantedId.isEmpty() ? m_player->position() : m_wantedStart},
        {QStringLiteral("request"), m_queueRequest},
        {QStringLiteral("continuation"), m_continuation},
        {QStringLiteral("automixPlaylistId"), m_automixPlaylistId},
        {QStringLiteral("automixParams"), m_automixParams},
    };
    file.write(QJsonDocument(session).toJson(QJsonDocument::Compact));
    file.commit();
}

void Playback::restoreSession()
{
    QFile file(sessionPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject session = QJsonDocument::fromJson(file.readAll()).object();
    const QVariantList items = session.value(u"items").toArray().toVariantList();
    if (items.isEmpty())
        return;

    m_queueRequest = session.value(u"request").toObject();
    m_continuation = session.value(u"continuation").toString();
    m_automixPlaylistId = session.value(u"automixPlaylistId").toString();
    m_automixParams = session.value(u"automixParams").toString();
    m_queue->setItems(items, session.value(u"current").toInt());
    startCurrent(session.value(u"position").toDouble(), true);
}
