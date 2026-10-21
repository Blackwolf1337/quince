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
    return QStringLiteral("%1 %2 kbit/s").arg(stream.codec).arg(qRound(stream.bitrate));
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
    return m_queue->current() + 1 < m_queue->count();
}

bool Playback::hasPrevious() const
{
    return m_queue->current() > 0;
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
        startCurrent();

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

void Playback::requestPlayerInfo(const QString &videoId)
{
    if (videoId.isEmpty() || m_infoRequested.contains(videoId))
        return;
    if (m_infoRequested.size() > kMaxPlayerInfo) {
        m_infoRequested.clear();
        m_loudness.clear();
        m_tracking.clear();
    }
    m_infoRequested.insert(videoId);

    // any recent day works as signature time for these fields
    const qint64 signatureTimestamp = QDateTime::currentSecsSinceEpoch() / 86400 - 1;
    const QJsonObject body{
        {QStringLiteral("video_id"), videoId},
        {QStringLiteral("playbackContext"),
         QJsonObject{{QStringLiteral("contentPlaybackContext"),
                      QJsonObject{{QStringLiteral("signatureTimestamp"), signatureTimestamp}}}}},
    };
    m_innerTube->post(QStringLiteral("player"), body, this,
                      [this, videoId](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty()) {
            m_infoRequested.remove(videoId);
            return;
        }
        const QJsonValue loudness = ytm::at(response, {"playerConfig", "audioConfig", "loudnessDb"});
        if (loudness.isDouble())
            m_loudness.insert(videoId, loudness.toDouble());
        const QString tracking = ytm::at(response, {"playbackTracking", "videostatsPlaybackUrl", "baseUrl"}).toString();
        if (!tracking.isEmpty())
            m_tracking.insert(videoId, tracking);
        // the stream may have started before this arrived
        if (videoId == m_loadedId)
            m_player->setGain(gainFor(videoId));
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
        startCurrent();
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
    m_resolver->resolve(m_wantedId);
    requestPlayerInfo(m_wantedId);
}

void Playback::prefetchUpcoming()
{
    const int current = m_queue->current();
    if (current >= m_queue->count() - kQueueLowWater)
        loadMoreQueue();

    // only once the current track is in mpv, so the order stays right
    if (m_loadedId.isEmpty() || m_loadedId != m_queue->videoIdAt(current))
        return;

    const QString upcoming = m_queue->videoIdAt(current + 1);
    if (upcoming == m_queuedId)
        return;
    if (!m_queuedId.isEmpty()) {
        m_player->clearQueued();
        m_queuedId.clear();
    }
    if (!upcoming.isEmpty()) {
        m_resolver->resolve(upcoming);
        requestPlayerInfo(upcoming);
    }
}

void Playback::onResolved(const Stream &stream)
{
    if (stream.videoId == m_wantedId) {
        m_wantedId.clear();
        m_failures = 0;
        setLoading(false);
        m_player->load(stream, stream.videoId, m_wantedStart, m_wantedPaused, gainFor(stream.videoId));
        m_loadedId = stream.videoId;
        m_playingFor.start();
        m_queuedId.clear();
        setStreamInfo(streamInfoOf(stream));
        prefetchUpcoming();
        return;
    }

    const int current = m_queue->current();
    if (stream.videoId == m_queue->videoIdAt(current + 1) && m_loadedId == m_queue->videoIdAt(current)
        && m_queuedId != stream.videoId) {
        m_player->queueNext(stream, stream.videoId, gainFor(stream.videoId));
        m_queuedId = stream.videoId;
        m_queuedInfo = streamInfoOf(stream);
    }
}

void Playback::onFailed(const QString &videoId, const QString &error)
{
    if (videoId != m_wantedId)
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
    setStreamInfo(m_queuedInfo);
    const int next = m_queue->current() + 1;
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
        // googlevideo refuses some URLs (403); a fresh one from yt-dlp usually works
        m_resolver->forget(tag);
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
    startCurrent();
}

void Playback::next()
{
    if (hasNext())
        playIndex(m_queue->current() + 1);
}

void Playback::previous()
{
    if (m_player->position() > kRestartThreshold || !hasPrevious()) {
        m_player->seek(0);
        return;
    }
    playIndex(m_queue->current() - 1);
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
