#include "lyrics/Lyrics.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "ytm/InnerTube.h"
#include "ytm/Json.h"
#include "ytm/Parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcLyrics, "quince.lyrics")

namespace {

const QString kLrclib = QStringLiteral("https://lrclib.net/api/");
// Genius serves its pages to browsers
const QByteArray kBrowserAgent =
    QByteArrayLiteral("Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0");
// LRCLIB asks clients to name themselves
const QByteArray kUserAgent = QByteArrayLiteral("Quince/" PROJECT_VERSION " (native YouTube Music player)");
// a search hit may differ this much in length and still be the same recording
constexpr double kDurationTolerance = 3.0;
// lines are shown this early, the eye is slower than the ear
constexpr double kLead = 0.25;

QString firstArtist(const QVariantMap &track)
{
    const QVariantList artists = track.value(QStringLiteral("artists")).toList();
    if (!artists.isEmpty())
        return artists.first().toMap().value(QStringLiteral("name")).toString();
    return track.value(QStringLiteral("artistText")).toString();
}

} // namespace

Lyrics::Lyrics(Playback *playback, MpvPlayer *player, InnerTube *innerTube, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_innerTube(innerTube)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_playback, &Playback::trackChanged, this, &Lyrics::onTrackChanged);
    connect(m_player, &MpvPlayer::positionChanged, this, &Lyrics::updateCurrentLine);
}

QString Lyrics::cleanTitle(const QString &title)
{
    static const QRegularExpression noise(QStringLiteral(
        "\\s*[\\(\\[](official\\s*(music\\s*)?(video|audio|visualizer|lyric\\s*video)|lyrics?|audio|video|"
        "hd|hq|4k|explicit|visualizer)[\\)\\]]"), QRegularExpression::CaseInsensitiveOption);
    QString clean = title;
    clean.remove(noise);
    return clean.trimmed();
}

QVariantList Lyrics::parseLrc(const QString &lrc)
{
    static const QRegularExpression stamp(QStringLiteral("\\[(\\d+):(\\d+(?:[.:]\\d+)?)\\]"));
    static const QRegularExpression offsetTag(QStringLiteral("^\\[offset:\\s*([+-]?\\d+)\\]"),
                                              QRegularExpression::CaseInsensitiveOption);
    double offset = 0;
    QList<QPair<double, QString>> lines;
    for (const QString &raw : lrc.split(u'\n')) {
        const QString line = raw.trimmed();
        if (const QRegularExpressionMatch m = offsetTag.match(line); m.hasMatch()) {
            offset = m.captured(1).toDouble() / 1000.0;
            continue;
        }
        QList<double> times;
        qsizetype end = 0;
        for (auto it = stamp.globalMatch(line); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedStart() != end)
                break;
            QString seconds = m.captured(2);
            seconds.replace(u':', u'.');
            times.append(m.captured(1).toDouble() * 60.0 + seconds.toDouble());
            end = m.capturedEnd();
        }
        if (times.isEmpty())
            continue;
        const QString text = line.mid(end).trimmed();
        for (double t : times)
            lines.append({qMax(0.0, t - offset), text});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    QVariantList out;
    for (const auto &[time, text] : lines)
        out.append(QVariantMap{{QStringLiteral("time"), time}, {QStringLiteral("text"), text}});
    return out;
}

void Lyrics::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void Lyrics::setResult(const QVariantList &lines, bool synced, const QString &source)
{
    if (!source.isEmpty())
        qCDebug(lcLyrics) << lines.size() << "lines from" << source << (synced ? "synced" : "");
    m_lines = lines;
    m_synced = synced;
    m_source = source;
    m_times.clear();
    for (const QVariant &v : lines)
        m_times.append(v.toMap().value(QStringLiteral("time")).toDouble());
    m_currentLine = -1;
    setLoading(false);
    emit linesChanged();
    emit currentLineChanged();
    updateCurrentLine();
}

void Lyrics::updateCurrentLine()
{
    if (!m_synced || m_times.isEmpty())
        return;
    const double position = m_player->position() + kLead;
    // last line that started at or before the position
    const auto it = std::upper_bound(m_times.cbegin(), m_times.cend(), position);
    const int line = static_cast<int>(it - m_times.cbegin()) - 1;
    if (line != m_currentLine) {
        m_currentLine = line;
        emit currentLineChanged();
    }
}

void Lyrics::onTrackChanged()
{
    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    // the same track again, e.g. after its queue entry got more details
    if (videoId == m_videoId)
        return;
    m_videoId = videoId;
    const int generation = ++m_generation;
    setResult({}, false, {});
    if (videoId.isEmpty())
        return;

    setLoading(true);
    // QUINCE_LYRICS=genius asks Genius first (test runs of that source)
    if (qEnvironmentVariable("QUINCE_LYRICS") == u"genius")
        fetchGenius(track, generation);
    else if (!m_settings->lyricsLrclib())
        fetchYouTube(track, generation);
    else if (track.value(QStringLiteral("durationSeconds")).toInt() > 0)
        fetchLrclib(track, generation);
    else
        // the exact lookup needs the length; quick picks come without it
        searchLrclib(track, generation);
}

void Lyrics::fetchLrclib(const QVariantMap &track, int generation)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), cleanTitle(track.value(QStringLiteral("title")).toString()));
    query.addQueryItem(QStringLiteral("artist_name"), firstArtist(track));
    const QString album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    if (!album.isEmpty())
        query.addQueryItem(QStringLiteral("album_name"), album);
    query.addQueryItem(QStringLiteral("duration"), track.value(QStringLiteral("durationSeconds")).toString());
    QUrl url(kLrclib + QStringLiteral("get"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, track, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
        const QString synced = result.value(u"syncedLyrics").toString();
        if (reply->error() == QNetworkReply::NoError && !synced.isEmpty()) {
            setResult(parseLrc(synced), true, QStringLiteral("LRCLIB"));
            return;
        }
        // no exact match: the album or the length may differ a little
        searchLrclib(track, generation);
    });
}

void Lyrics::searchLrclib(const QVariantMap &track, int generation)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), cleanTitle(track.value(QStringLiteral("title")).toString()));
    query.addQueryItem(QStringLiteral("artist_name"), firstArtist(track));
    QUrl url(kLrclib + QStringLiteral("search"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    QNetworkReply *reply = m_network->get(request);
    const double duration = track.value(QStringLiteral("durationSeconds")).toDouble();
    connect(reply, &QNetworkReply::finished, this, [this, reply, duration, track, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        QString best;
        double bestDiff = kDurationTolerance + 1;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject hit = v.toObject();
            const QString synced = hit.value(u"syncedLyrics").toString();
            // without a known length the first synced hit is taken
            const double diff = duration > 0 ? std::abs(hit.value(u"duration").toDouble() - duration) : 0.0;
            if (!synced.isEmpty() && diff <= kDurationTolerance && diff < bestDiff) {
                best = synced;
                bestDiff = diff;
            }
        }
        if (!best.isEmpty()) {
            setResult(parseLrc(best), true, QStringLiteral("LRCLIB"));
            return;
        }
        fetchYouTube(track, generation);
    });
}

// The lyrics tab of the site: its browse id comes with the next answer.
void Lyrics::fetchYouTube(const QVariantMap &track, int generation)
{
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    m_innerTube->post(QStringLiteral("next"), {{QStringLiteral("videoId"), videoId}}, this,
                      [this, track, generation](const QJsonObject &response, const QString &error) {
        if (generation != m_generation)
            return;
        const QString browseId = ytm::parseWatchNext(response).lyricsBrowseId;
        if (!error.isEmpty() || browseId.isEmpty()) {
            fetchGenius(track, generation);
            return;
        }
        m_innerTube->post(QStringLiteral("browse"), {{QStringLiteral("browseId"), browseId}}, this,
                          [this, track, generation](const QJsonObject &page, const QString &error) {
            if (generation != m_generation)
                return;
            // the lyrics page is a bare section list with one text shelf
            const QJsonValue shelf = ytm::at(page, {"contents", "sectionListRenderer", "contents", 0,
                                                    "musicDescriptionShelfRenderer"});
            const QString text = ytm::textOf(ytm::at(shelf, {"description"}));
            // "Source: LyricFind" and the like, below the text
            const QString source = ytm::textOf(ytm::at(shelf, {"footer"}));
            if (!error.isEmpty() || text.isEmpty()) {
                fetchGenius(track, generation);
                return;
            }
            QVariantList lines;
            for (const QString &line : text.split(u'\n'))
                lines.append(QVariantMap{{QStringLiteral("time"), -1}, {QStringLiteral("text"), line.trimmed()}});
            setResult(lines, false, source.isEmpty() ? QStringLiteral("YouTube Music") : source);
        });
    });
}

namespace {

// the end of the element that starts at from (an opening <div ...>), by
// counting the divs inside it; -1 when it does not close
qsizetype elementEnd(const QString &html, qsizetype from)
{
    static const QRegularExpression div(QStringLiteral("<(/?)div\\b[^>]*>"));
    int depth = 0;
    QRegularExpressionMatchIterator it = div.globalMatch(html, from);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        depth += m.captured(1).isEmpty() ? 1 : -1;
        if (depth == 0)
            return m.capturedEnd();
    }
    return -1;
}

QString unescape(QString text)
{
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = numeric.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(text).mid(last, m.capturedStart() - last);
        out += QChar(char16_t(m.captured(2).toUInt(nullptr, m.captured(1).isEmpty() ? 10 : 16)));
        last = m.capturedEnd();
    }
    out += QStringView(text).mid(last);
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return out;
}

// the text of a Genius song page: its data-lyrics-container parts, without
// the parts marked data-exclude-from-selection (the header inside them)
QString geniusLyrics(const QString &html)
{
    static const QRegularExpression container(QStringLiteral("<div[^>]*data-lyrics-container=\"true\"[^>]*>"));
    static const QRegularExpression excluded(QStringLiteral("<div[^>]*data-exclude-from-selection=\"true\"[^>]*>"));
    static const QRegularExpression br(QStringLiteral("<br\\s*/?>"));
    static const QRegularExpression tag(QStringLiteral("<[^>]+>"));
    QString text;
    QRegularExpressionMatchIterator it = container.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype end = elementEnd(html, m.capturedStart());
        if (end < 0)
            continue;
        QString part = html.mid(m.capturedEnd(), end - m.capturedEnd() - 6);
        for (QRegularExpressionMatch x = excluded.match(part); x.hasMatch(); x = excluded.match(part)) {
            const qsizetype xEnd = elementEnd(part, x.capturedStart());
            part.remove(x.capturedStart(), (xEnd < 0 ? part.size() : xEnd) - x.capturedStart());
        }
        part.replace(br, QStringLiteral("\n"));
        part.remove(tag);
        if (!text.isEmpty())
            text += u'\n';
        text += unescape(part);
    }
    return text.trimmed();
}

} // namespace

// Genius, when LRCLIB and YouTube have nothing: its search, then the song
// page (unsynced text)
void Lyrics::fetchGenius(const QVariantMap &track, int generation)
{
    if (!m_settings->lyricsGenius()) {
        setResult({}, false, {});
        return;
    }
    const QString artist = firstArtist(track);
    QUrl url(QStringLiteral("https://genius.com/api/search/multi"));
    url.setQuery(QUrlQuery{{QStringLiteral("q"), artist + u' ' + cleanTitle(track.value(QStringLiteral("title")).toString())}});
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kBrowserAgent);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        QString page;
        const QJsonArray sections = ytm::at(QJsonDocument::fromJson(reply->readAll()).object(),
                                            {"response", "sections"}).toArray();
        for (const QJsonValue &section : sections) {
            if (section[u"type"].toString() != u"song")
                continue;
            for (const QJsonValue &hit : section[u"hits"].toArray()) {
                const QJsonValue result = hit[u"result"];
                const QString by = ytm::at(result, {"primary_artist", "name"}).toString();
                // the right artist, not a cover or a translation page
                if (!artist.isEmpty() && !by.contains(artist, Qt::CaseInsensitive)
                    && !artist.contains(by, Qt::CaseInsensitive))
                    continue;
                page = result[u"url"].toString();
                break;
            }
            break;
        }
        if (page.isEmpty()) {
            setResult({}, false, {});
            return;
        }
        QNetworkRequest pageRequest{QUrl(page)};
        pageRequest.setHeader(QNetworkRequest::UserAgentHeader, kBrowserAgent);
        QNetworkReply *pageReply = m_network->get(pageRequest);
        connect(pageReply, &QNetworkReply::finished, this, [this, pageReply, generation] {
            pageReply->deleteLater();
            if (generation != m_generation)
                return;
            const QString text = geniusLyrics(QString::fromUtf8(pageReply->readAll()));
            if (text.isEmpty()) {
                setResult({}, false, {});
                return;
            }
            QVariantList lines;
            for (const QString &line : text.split(u'\n'))
                lines.append(QVariantMap{{QStringLiteral("time"), -1}, {QStringLiteral("text"), line.trimmed()}});
            setResult(lines, false, QStringLiteral("Genius"));
        });
    });
}
