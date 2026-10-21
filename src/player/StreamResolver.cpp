#include "player/StreamResolver.h"
#include "player/JsSolver.h"
#include "ytm/AccountCookies.h"
#include "ytm/InnerTube.h"
#include "ytm/Json.h"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcResolve, "quince.resolve")

namespace {

// a URL closer than this to its expire time is resolved again
constexpr qint64 kExpireMarginSecs = 10 * 60;

// 774 is the 256 kbit/s Opus format YouTube Music serves to Premium accounts,
// 251 the 160 kbit/s Opus format everybody gets.
const QList<int> kAudioFormats{774, 251, 250, 140, 249, 139};
constexpr int kPremiumFormat = 774;

const QByteArray kVisionUserAgent = QByteArrayLiteral(
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15");

// VISIONOS as yt-dlp 2026.08.19 sends it: no JS player, no PO token, no account
ForeignClient visionClient()
{
    return ForeignClient{
        QJsonObject{
            {QStringLiteral("clientName"), QStringLiteral("VISIONOS")},
            {QStringLiteral("clientVersion"), QStringLiteral("1.02")},
            {QStringLiteral("deviceMake"), QStringLiteral("Apple")},
            {QStringLiteral("deviceModel"), QStringLiteral("RealityDevice17,1")},
            {QStringLiteral("osName"), QStringLiteral("visionOS")},
            {QStringLiteral("osVersion"), QStringLiteral("26.5.23O471")},
            {QStringLiteral("userAgent"), QString::fromLatin1(kVisionUserAgent)},
        },
        QByteArrayLiteral("101"),
        kVisionUserAgent,
    };
}

QJsonObject playerBody(const QString &videoId, int signatureTimestamp)
{
    // Without a signatureTimestamp WEB_REMIX calls every track unavailable.
    // It counts days since 1970; yesterday serves for loudness and tracking
    // while no player is solved (its URLs are not used then).
    if (signatureTimestamp <= 0)
        signatureTimestamp = int(QDateTime::currentSecsSinceEpoch() / 86400 - 1);
    const QJsonObject playback{
        {QStringLiteral("html5Preference"), QStringLiteral("HTML5_PREF_WANTS")},
        {QStringLiteral("signatureTimestamp"), signatureTimestamp},
    };
    return QJsonObject{
        {QStringLiteral("videoId"), videoId},
        {QStringLiteral("playbackContext"), QJsonObject{{QStringLiteral("contentPlaybackContext"), playback}}},
        {QStringLiteral("contentCheckOk"), true},
        {QStringLiteral("racyCheckOk"), true},
    };
}

QJsonObject visionBody(const QString &videoId)
{
    return QJsonObject{
        {QStringLiteral("videoId"), videoId},
        {QStringLiteral("contentCheckOk"), true},
        {QStringLiteral("racyCheckOk"), true},
    };
}

bool playable(const QJsonObject &answer)
{
    return ytm::at(answer, {"playabilityStatus", "status"}).toString() == u"OK";
}

QString unplayableReason(const QJsonObject &answer)
{
    const QJsonValue status = ytm::at(answer, {"playabilityStatus"});
    QString reason = ytm::at(status, {"reason"}).toString();
    if (reason.isEmpty())
        reason = ytm::textOf(ytm::at(status, {"errorScreen", "playerErrorMessageRenderer", "reason"}));
    return reason;
}

// the best audio format by kAudioFormats, else the audio format with the highest bitrate
QJsonObject pickAudio(const QJsonObject &answer)
{
    QJsonObject best;
    int bestRank = INT_MAX;
    for (const QJsonValue &value : ytm::at(answer, {"streamingData", "adaptiveFormats"}).toArray()) {
        const QJsonObject format = value.toObject();
        if (!format.value(u"mimeType").toString().startsWith(u"audio/"))
            continue;
        if (format.contains(u"drmFamilies"))
            continue;
        const int known = kAudioFormats.indexOf(format.value(u"itag").toInt());
        const int rank = known >= 0 ? known : 100 - qMin(99, format.value(u"bitrate").toInt() / 10000);
        if (rank < bestRank) {
            bestRank = rank;
            best = format;
        }
    }
    return best;
}

// the best picture up to maxHeight: highest first, then VP9, H.264, AV1
QJsonObject pickVideo(const QJsonObject &answer, int maxHeight)
{
    const auto codecRank = [](const QString &mime) {
        return mime.contains(u"vp9") || mime.contains(u"vp09") ? 0 : mime.contains(u"avc1") ? 1 : 2;
    };
    QJsonObject best;
    for (const QJsonValue &value : ytm::at(answer, {"streamingData", "adaptiveFormats"}).toArray()) {
        const QJsonObject format = value.toObject();
        const QString mime = format.value(u"mimeType").toString();
        if (!mime.startsWith(u"video/") || format.contains(u"drmFamilies"))
            continue;
        const int height = format.value(u"height").toInt();
        if (height <= 0 || height > maxHeight)
            continue;
        if (best.isEmpty()) {
            best = format;
            continue;
        }
        const int bestHeight = best.value(u"height").toInt();
        const int bestRank = codecRank(best.value(u"mimeType").toString());
        if (height > bestHeight || (height == bestHeight && codecRank(mime) < bestRank)
            || (height == bestHeight && codecRank(mime) == bestRank
                && format.value(u"fps").toInt() > best.value(u"fps").toInt()))
            best = format;
    }
    return best;
}

bool hasFormat(const QJsonObject &answer, int itag)
{
    for (const QJsonValue &value : ytm::at(answer, {"streamingData", "adaptiveFormats"}).toArray()) {
        if (value[u"itag"].toInt() == itag)
            return true;
    }
    return false;
}

// "s=...&sp=sig&url=https%3A..." of a signatureCipher
QHash<QString, QString> formDecode(const QString &text)
{
    QHash<QString, QString> out;
    for (const QString &pair : text.split(u'&', Qt::SkipEmptyParts)) {
        const qsizetype eq = pair.indexOf(u'=');
        if (eq <= 0)
            continue;
        QByteArray value = pair.mid(eq + 1).toUtf8();
        value.replace('+', ' ');
        out.insert(pair.left(eq), QUrl::fromPercentEncoding(value));
    }
    return out;
}

// url with the query item key set to value (added when missing)
QUrl withQueryItem(const QUrl &url, const QString &key, const QString &value)
{
    QStringList items;
    bool replaced = false;
    const QByteArray encoded = QUrl::toPercentEncoding(value);
    for (const QString &item : url.query(QUrl::FullyEncoded).split(u'&', Qt::SkipEmptyParts)) {
        if (item.section(u'=', 0, 0) == key) {
            items.append(key + u'=' + QString::fromLatin1(encoded));
            replaced = true;
        } else {
            items.append(item);
        }
    }
    if (!replaced)
        items.append(key + u'=' + QString::fromLatin1(encoded));
    QUrl out(url);
    out.setQuery(items.join(u'&'), QUrl::StrictMode);
    return out;
}

QString codecOf(const QString &mimeType)
{
    static const QRegularExpression codecs(QStringLiteral("codecs=\"([^\".]+)"));
    const QString codec = codecs.match(mimeType).captured(1);
    return codec == u"mp4a" ? QStringLiteral("aac") : codec;
}

// QUINCE_CLIENT=web or vision forces one client (test runs, diagnosis); web
// then also goes without an account and without 774
QString forcedClient()
{
    static const QString forced = qEnvironmentVariable("QUINCE_CLIENT");
    return forced;
}

} // namespace

StreamResolver::StreamResolver(InnerTube *innerTube, JsSolver *solver, QObject *parent)
    : QObject(parent)
    , m_innerTube(innerTube)
    , m_solver(solver)
{
    // Tracks that waited for a solved player ask again: their first request
    // carried no signatureTimestamp of a real player.
    connect(m_solver, &JsSolver::prepared, this, [this] {
        const QSet<QString> waiting = std::exchange(m_waiting, {});
        for (const QString &key : waiting) {
            const Pending pending = m_pending.take(key);
            if (m_solver->ready())
                resolve(pending.videoId, pending.video);
            else
                fail(pending.videoId, tr("YouTube's player could not be loaded."));
        }
    });
}

StreamResolver::~StreamResolver() = default;

void StreamResolver::setCookies(AccountCookies *cookies)
{
    m_cookies = cookies;
}

void StreamResolver::setVideoHeight(int height)
{
    if (height == m_videoHeight)
        return;
    m_videoHeight = height;
    // video URLs of the old quality
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->video)
            it = m_cache.erase(it);
        else
            ++it;
    }
}

QString StreamResolver::keyOf(const QString &videoId, bool video)
{
    return video ? videoId + QStringLiteral("#v") : videoId;
}

bool StreamResolver::isCached(const QString &videoId, bool video) const
{
    const auto it = m_cache.constFind(keyOf(videoId, video));
    if (it == m_cache.cend())
        return false;
    return QDateTime::currentDateTimeUtc().secsTo(it->expires) > kExpireMarginSecs;
}

void StreamResolver::forget(const QString &videoId)
{
    for (const bool video : {false, true}) {
        const auto it = m_cache.constFind(keyOf(videoId, video));
        if (it == m_cache.cend())
            continue;
        if (it->client == u"WEB_REMIX") {
            m_webFailed.insert(videoId);
            // a refused URL may mean a new player; look again
            m_solver->prepare(true);
        }
        m_cache.erase(it);
    }
}

bool StreamResolver::isRunning(const QString &videoId, bool video) const
{
    return m_pending.contains(keyOf(videoId, video));
}

void StreamResolver::fail(const QString &videoId, const QString &error)
{
    qCWarning(lcResolve) << videoId << error;
    emit failed(videoId, error);
}

void StreamResolver::resolve(const QString &videoId, bool video)
{
    const QString key = keyOf(videoId, video);
    if (isCached(videoId, video)) {
        QMetaObject::invokeMethod(this, [this, key] {
            emit resolved(m_cache.value(key));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pending.contains(key))
        return;

    Pending &pending = m_pending[key];
    pending.videoId = videoId;
    pending.video = video;
    pending.webAllowed = forcedClient() == u"web" || (!m_webFailed.contains(videoId) && forcedClient() != u"vision");
    const bool premiumLikely = m_cookies && m_cookies->signedIn();
    if (pending.webAllowed && (premiumLikely || forcedClient() == u"web")) {
        // loads the player JS while the requests are under way
        m_solver->warm();
    }
    pending.playerId = m_solver->playerId();
    qCDebug(lcResolve) << "resolving" << videoId << (video ? "video" : "audio") << "web" << pending.webAllowed;

    m_innerTube->post(QStringLiteral("player"), playerBody(videoId, m_solver->signatureTimestamp()), this,
                      [this, key](const QJsonObject &answer, const QString &error) {
        auto it = m_pending.find(key);
        if (it == m_pending.end())
            return;
        it->webDone = true;
        it->web = answer;
        it->webError = error;
        if (!it->visionSent)
            requestVision(key);
        onAnswer(key);
    });
    // the first request of a run waits for the visitor id the answer above brings
    if (m_innerTube->hasVisitorData())
        requestVision(key);
}

void StreamResolver::requestVision(const QString &key)
{
    Pending &pending = m_pending[key];
    pending.visionSent = true;
    m_innerTube->postAs(visionClient(), QStringLiteral("player"), visionBody(pending.videoId), this,
                        [this, key](const QJsonObject &answer, const QString &error) {
        auto it = m_pending.find(key);
        if (it == m_pending.end())
            return;
        it->visionDone = true;
        it->vision = answer;
        it->visionError = error;
        onAnswer(key);
    });
}

void StreamResolver::onAnswer(const QString &key)
{
    const Pending pending = m_pending.value(key);
    if (!pending.webDone || !pending.visionDone)
        return;

    // Premium: 774 through WEB_REMIX. VISIONOS refusals (age gate, YouTube's
    // bot check) go through WEB_REMIX too.
    const bool web = pending.webAllowed && !pending.playerId.isEmpty() && playable(pending.web)
        && (hasFormat(pending.web, kPremiumFormat) || !playable(pending.vision) || forcedClient() == u"web");
    if (web && finishWeb(key, pending))
        return;
    if (finishVision(key, pending))
        return;
    if (pending.webAllowed && playable(pending.web) && pending.playerId.isEmpty() && m_solver->preparing()) {
        m_waiting.insert(key);
        return;
    }

    m_pending.remove(key);
    QString reason = unplayableReason(pending.web);
    if (reason.isEmpty())
        reason = unplayableReason(pending.vision);
    if (reason.isEmpty())
        reason = !pending.webError.isEmpty() ? pending.webError : pending.visionError;
    if (reason.isEmpty())
        reason = tr("YouTube gave no audio stream.");
    fail(pending.videoId, reason);
}

// the stream's formats of an answer: audio, and in video mode the picture
bool StreamResolver::pickFormats(const QJsonObject &answer, bool video, QJsonObject *audio, QJsonObject *picture) const
{
    *audio = pickAudio(answer);
    if (audio->isEmpty())
        return false;
    if (video) {
        *picture = pickVideo(answer, m_videoHeight);
        // no picture to have: the sound alone
    }
    return true;
}

bool StreamResolver::finishWeb(const QString &key, const Pending &pending)
{
    QJsonObject audio;
    QJsonObject picture;
    if (!pickFormats(pending.web, pending.video, &audio, &picture))
        return false;

    struct Part
    {
        QUrl url;
        QString n;
        QString signature;
        QString signatureKey;
    };
    const auto partOf = [](const QJsonObject &format) {
        Part part;
        part.url = QUrl(format.value(u"url").toString());
        if (part.url.isEmpty()) {
            const QHash<QString, QString> cipher = formDecode(format.value(u"signatureCipher").toString());
            part.url = QUrl(cipher.value(QStringLiteral("url")));
            part.signature = cipher.value(QStringLiteral("s"));
            part.signatureKey = cipher.value(QStringLiteral("sp"), QStringLiteral("signature"));
        }
        part.n = QUrlQuery(part.url).queryItemValue(QStringLiteral("n"), QUrl::FullyDecoded);
        return part;
    };
    QList<Part> parts{partOf(audio)};
    if (!picture.isEmpty())
        parts.append(partOf(picture));

    Stream stream = streamOf(pending.videoId, QStringLiteral("WEB_REMIX"), audio, picture);
    stream.videoAsked = pending.video;
    stream.headers = {
        {QByteArrayLiteral("User-Agent"), InnerTube::userAgent()},
        {QByteArrayLiteral("Origin"), QByteArrayLiteral("https://music.youtube.com")},
        {QByteArrayLiteral("Referer"), QByteArrayLiteral("https://music.youtube.com/")},
    };

    QStringList nList;
    QStringList sigList;
    for (const Part &part : parts) {
        if (!part.n.isEmpty() && !nList.contains(part.n))
            nList.append(part.n);
        if (!part.signature.isEmpty())
            sigList.append(part.signature);
    }

    m_solver->solve(pending.playerId, nList, sigList, this,
                    [this, key, pending, stream, parts](const QHash<QString, QString> &nOut,
                                                        const QHash<QString, QString> &sigOut,
                                                        const QString &error) mutable {
        QList<QUrl> urls;
        QString problem = error;
        for (const Part &part : parts) {
            QUrl url = part.url;
            if (!part.n.isEmpty()) {
                const QString solved = nOut.value(part.n);
                if (solved.isEmpty())
                    problem = QStringLiteral("n not solved");
                url = withQueryItem(url, QStringLiteral("n"), solved);
            }
            if (!part.signature.isEmpty()) {
                const QString solved = sigOut.value(part.signature);
                if (solved.isEmpty())
                    problem = QStringLiteral("signature not solved");
                url = withQueryItem(url, part.signatureKey, solved);
            }
            urls.append(url);
        }
        if (!problem.isEmpty()) {
            qCWarning(lcResolve) << pending.videoId << "JS solve failed, taking VISIONOS:" << problem;
            if (!finishVision(key, pending)) {
                m_pending.remove(key);
                fail(pending.videoId, tr("YouTube's player puzzle could not be solved: %1").arg(problem));
            }
            return;
        }
        if (stream.video) {
            stream.audioUrl = urls.at(0);
            stream.url = urls.at(1);
        } else {
            stream.url = urls.at(0);
        }
        deliver(key, stream, pending);
    });
    return true;
}

bool StreamResolver::finishVision(const QString &key, const Pending &pending)
{
    if (!playable(pending.vision))
        return false;
    QJsonObject audio;
    QJsonObject picture;
    if (!pickFormats(pending.vision, pending.video, &audio, &picture))
        return false;
    Stream stream = streamOf(pending.videoId, QStringLiteral("VISIONOS"), audio, picture);
    stream.videoAsked = pending.video;
    if (stream.video) {
        stream.audioUrl = QUrl(audio.value(u"url").toString());
        stream.url = QUrl(picture.value(u"url").toString());
    } else {
        stream.url = QUrl(audio.value(u"url").toString());
    }
    if (stream.url.isEmpty())
        return false;
    stream.headers = {{QByteArrayLiteral("User-Agent"), kVisionUserAgent}};
    deliver(key, stream, pending);
    return true;
}

Stream StreamResolver::streamOf(const QString &videoId, const QString &client, const QJsonObject &audio,
                                const QJsonObject &picture)
{
    Stream stream;
    stream.videoId = videoId;
    stream.client = client;
    stream.formatId = QString::number(audio.value(u"itag").toInt());
    stream.codec = codecOf(audio.value(u"mimeType").toString());
    stream.bitrate = audio.value(u"averageBitrate").toDouble(audio.value(u"bitrate").toDouble()) / 1000;
    stream.duration = audio.value(u"approxDurationMs").toString().toDouble() / 1000;
    if (!picture.isEmpty()) {
        stream.video = true;
        stream.height = picture.value(u"height").toInt();
        stream.formatId = QString::number(picture.value(u"itag").toInt()) + u'+' + stream.formatId;
    }
    return stream;
}

void StreamResolver::deliver(const QString &key, Stream stream, const Pending &pending)
{
    m_pending.remove(key);

    // the track itself is described best by YouTube Music's answer
    const QJsonObject &info = playable(pending.web) ? pending.web : pending.vision;
    const QJsonValue details = ytm::at(info, {"videoDetails"});
    stream.title = ytm::at(details, {"title"}).toString();
    stream.artist = ytm::at(details, {"author"}).toString();
    if (stream.artist.endsWith(u" - Topic"))
        stream.artist.chop(8);
    const QJsonArray thumbs = ytm::at(details, {"thumbnail", "thumbnails"}).toArray();
    if (!thumbs.isEmpty())
        stream.thumbnail = QUrl(thumbs.last()[u"url"].toString());
    if (stream.duration <= 0)
        stream.duration = ytm::at(details, {"lengthSeconds"}).toString().toDouble();

    const QJsonValue loudness = ytm::at(pending.web, {"playerConfig", "audioConfig", "loudnessDb"});
    if (loudness.isDouble())
        stream.loudnessDb = loudness.toDouble();
    stream.trackingUrl = ytm::at(pending.web, {"playbackTracking", "videostatsPlaybackUrl", "baseUrl"}).toString();

    if (stream.video) {
        // captions as WebVTT, which mpv reads
        const QJsonArray tracks = ytm::at(info, {"captions", "playerCaptionsTracklistRenderer", "captionTracks"}).toArray();
        for (const QJsonValue &track : tracks) {
            QUrl url(track[u"baseUrl"].toString());
            if (url.isRelative())
                url = QUrl(QStringLiteral("https://www.youtube.com")).resolved(url);
            url = withQueryItem(url, QStringLiteral("fmt"), QStringLiteral("vtt"));
            stream.captions.append(QVariantMap{
                {QStringLiteral("url"), url},
                {QStringLiteral("language"), track[u"languageCode"].toString()},
                {QStringLiteral("name"), ytm::textOf(track[u"name"])},
                {QStringLiteral("generated"), track[u"kind"].toString() == u"asr"},
            });
        }
    }

    const qint64 expire = QUrlQuery(stream.url).queryItemValue(QStringLiteral("expire")).toLongLong();
    stream.expires = expire > 0 ? QDateTime::fromSecsSinceEpoch(expire, QTimeZone::UTC)
                                : QDateTime::currentDateTimeUtc().addSecs(3600);

    qCDebug(lcResolve) << "resolved" << stream.videoId << stream.client << stream.formatId << stream.codec
                       << stream.bitrate << "kbit/s" << (stream.video ? QStringLiteral("%1p").arg(stream.height) : QString());
    m_cache.insert(key, stream);
    emit resolved(stream);
}

QString StreamResolver::extractVideoId(const QString &text)
{
    static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    static const QRegularExpression inUrl(
        QStringLiteral("(?:[?&]v=|youtu\\.be/|/shorts/|/embed/|/live/)([A-Za-z0-9_-]{11})"));

    const QString trimmed = text.trimmed();
    if (bare.match(trimmed).hasMatch())
        return trimmed;
    const QRegularExpressionMatch match = inUrl.match(trimmed);
    return match.hasMatch() ? match.captured(1) : QString();
}
