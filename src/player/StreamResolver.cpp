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
        for (const QString &videoId : waiting) {
            m_pending.remove(videoId);
            if (m_solver->ready())
                resolve(videoId);
            else
                fail(videoId, tr("YouTube's player could not be loaded."));
        }
    });
}

StreamResolver::~StreamResolver() = default;

void StreamResolver::setCookies(AccountCookies *cookies)
{
    m_cookies = cookies;
}

bool StreamResolver::isCached(const QString &videoId) const
{
    const auto it = m_cache.constFind(videoId);
    if (it == m_cache.cend())
        return false;
    return QDateTime::currentDateTimeUtc().secsTo(it->expires) > kExpireMarginSecs;
}

void StreamResolver::forget(const QString &videoId)
{
    const auto it = m_cache.constFind(videoId);
    if (it != m_cache.cend() && it->client == u"WEB_REMIX") {
        m_webFailed.insert(videoId);
        // a refused URL may mean a new player; look again
        m_solver->prepare(true);
    }
    m_cache.remove(videoId);
}

bool StreamResolver::isRunning(const QString &videoId) const
{
    return m_pending.contains(videoId);
}

void StreamResolver::fail(const QString &videoId, const QString &error)
{
    qCWarning(lcResolve) << videoId << error;
    emit failed(videoId, error);
}

void StreamResolver::resolve(const QString &videoId)
{
    if (isCached(videoId)) {
        QMetaObject::invokeMethod(this, [this, videoId] {
            emit resolved(m_cache.value(videoId));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pending.contains(videoId))
        return;

    Pending &pending = m_pending[videoId];
    pending.webAllowed = forcedClient() == u"web" || (!m_webFailed.contains(videoId) && forcedClient() != u"vision");
    const bool premiumLikely = m_cookies && m_cookies->signedIn();
    if (pending.webAllowed && (premiumLikely || forcedClient() == u"web")) {
        // loads the player JS while the requests are under way
        m_solver->warm();
    }
    pending.playerId = m_solver->playerId();
    qCDebug(lcResolve) << "resolving" << videoId << "web" << pending.webAllowed;

    m_innerTube->post(QStringLiteral("player"), playerBody(videoId, m_solver->signatureTimestamp()), this,
                      [this, videoId](const QJsonObject &answer, const QString &error) {
        auto it = m_pending.find(videoId);
        if (it == m_pending.end())
            return;
        it->webDone = true;
        it->web = answer;
        it->webError = error;
        if (!it->visionSent)
            requestVision(videoId);
        onAnswer(videoId);
    });
    // the first request of a run waits for the visitor id the answer above brings
    if (m_innerTube->hasVisitorData())
        requestVision(videoId);
}

void StreamResolver::requestVision(const QString &videoId)
{
    m_pending[videoId].visionSent = true;
    m_innerTube->postAs(visionClient(), QStringLiteral("player"), visionBody(videoId), this,
                        [this, videoId](const QJsonObject &answer, const QString &error) {
        auto it = m_pending.find(videoId);
        if (it == m_pending.end())
            return;
        it->visionDone = true;
        it->vision = answer;
        it->visionError = error;
        onAnswer(videoId);
    });
}

void StreamResolver::onAnswer(const QString &videoId)
{
    const Pending pending = m_pending.value(videoId);
    if (!pending.webDone || !pending.visionDone)
        return;

    // Premium: 774 through WEB_REMIX. VISIONOS refusals (age gate, YouTube's
    // bot check) go through WEB_REMIX too.
    const bool web = pending.webAllowed && !pending.playerId.isEmpty() && playable(pending.web)
        && (hasFormat(pending.web, kPremiumFormat) || !playable(pending.vision) || forcedClient() == u"web");
    if (web) {
        const QJsonObject format = pickAudio(pending.web);
        if (!format.isEmpty()) {
            finishWeb(videoId, pending, format);
            return;
        }
    }
    if (finishVision(videoId, pending))
        return;
    if (pending.webAllowed && playable(pending.web) && pending.playerId.isEmpty() && m_solver->preparing()) {
        m_waiting.insert(videoId);
        return;
    }

    m_pending.remove(videoId);
    QString reason = unplayableReason(pending.web);
    if (reason.isEmpty())
        reason = unplayableReason(pending.vision);
    if (reason.isEmpty())
        reason = !pending.webError.isEmpty() ? pending.webError : pending.visionError;
    if (reason.isEmpty())
        reason = tr("YouTube gave no audio stream.");
    fail(videoId, reason);
}

void StreamResolver::finishWeb(const QString &videoId, const Pending &pending, const QJsonObject &format)
{
    QUrl url(format.value(u"url").toString());
    QString signature;
    QString signatureKey;
    if (url.isEmpty()) {
        const QHash<QString, QString> cipher = formDecode(format.value(u"signatureCipher").toString());
        url = QUrl(cipher.value(QStringLiteral("url")));
        signature = cipher.value(QStringLiteral("s"));
        signatureKey = cipher.value(QStringLiteral("sp"), QStringLiteral("signature"));
    }
    const QString n = QUrlQuery(url).queryItemValue(QStringLiteral("n"), QUrl::FullyDecoded);

    Stream stream;
    stream.videoId = videoId;
    stream.client = QStringLiteral("WEB_REMIX");
    stream.formatId = QString::number(format.value(u"itag").toInt());
    stream.codec = codecOf(format.value(u"mimeType").toString());
    stream.bitrate = format.value(u"averageBitrate").toDouble(format.value(u"bitrate").toDouble()) / 1000;
    stream.duration = format.value(u"approxDurationMs").toString().toDouble() / 1000;
    stream.headers = {
        {QByteArrayLiteral("User-Agent"), InnerTube::userAgent()},
        {QByteArrayLiteral("Origin"), QByteArrayLiteral("https://music.youtube.com")},
        {QByteArrayLiteral("Referer"), QByteArrayLiteral("https://music.youtube.com/")},
    };
    stream.url = url;

    QStringList nList;
    if (!n.isEmpty())
        nList.append(n);
    QStringList sigList;
    if (!signature.isEmpty())
        sigList.append(signature);

    m_solver->solve(pending.playerId, nList, sigList, this,
                    [this, videoId, pending, stream, n, signature, signatureKey](
                        const QHash<QString, QString> &nOut, const QHash<QString, QString> &sigOut,
                        const QString &error) mutable {
        const QString solvedN = nOut.value(n);
        const QString solvedSig = sigOut.value(signature);
        if (!error.isEmpty() || (!n.isEmpty() && solvedN.isEmpty())
            || (!signature.isEmpty() && solvedSig.isEmpty())) {
            qCWarning(lcResolve) << videoId << "JS solve failed, taking VISIONOS:" << error;
            if (!finishVision(videoId, pending)) {
                m_pending.remove(videoId);
                fail(videoId, tr("YouTube's player puzzle could not be solved: %1").arg(error));
            }
            return;
        }
        if (!n.isEmpty())
            stream.url = withQueryItem(stream.url, QStringLiteral("n"), solvedN);
        if (!signature.isEmpty())
            stream.url = withQueryItem(stream.url, signatureKey, solvedSig);
        deliver(stream, pending);
    });
}

bool StreamResolver::finishVision(const QString &videoId, const Pending &pending)
{
    if (!playable(pending.vision))
        return false;
    const QJsonObject format = pickAudio(pending.vision);
    const QUrl url(format.value(u"url").toString());
    if (url.isEmpty())
        return false;

    Stream stream;
    stream.videoId = videoId;
    stream.client = QStringLiteral("VISIONOS");
    stream.url = url;
    stream.formatId = QString::number(format.value(u"itag").toInt());
    stream.codec = codecOf(format.value(u"mimeType").toString());
    stream.bitrate = format.value(u"averageBitrate").toDouble(format.value(u"bitrate").toDouble()) / 1000;
    stream.duration = format.value(u"approxDurationMs").toString().toDouble() / 1000;
    stream.headers = {{QByteArrayLiteral("User-Agent"), kVisionUserAgent}};
    deliver(stream, pending);
    return true;
}

void StreamResolver::deliver(Stream stream, const Pending &pending)
{
    m_pending.remove(stream.videoId);

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

    const qint64 expire = QUrlQuery(stream.url).queryItemValue(QStringLiteral("expire")).toLongLong();
    stream.expires = expire > 0 ? QDateTime::fromSecsSinceEpoch(expire, QTimeZone::UTC)
                                : QDateTime::currentDateTimeUtc().addSecs(3600);

    qCDebug(lcResolve) << "resolved" << stream.videoId << stream.client << stream.formatId << stream.codec
                       << stream.bitrate << "kbit/s";
    m_cache.insert(stream.videoId, stream);
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
