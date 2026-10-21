#include "integrations/SponsorBlock.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcSponsor, "quince.sponsorblock")

namespace {

const QString kApi = QStringLiteral("https://sponsor.ajay.app/api/skipSegments/");
// the kinds of parts skipped in music: no music, sponsor spots, self
// promotion, "subscribe" reminders
const QStringList kCategories{
    QStringLiteral("music_offtopic"), QStringLiteral("sponsor"), QStringLiteral("selfpromo"),
    QStringLiteral("interaction"),
};
// a segment that ends this close to where it starts is not worth a seek
constexpr double kMinimumSkip = 1.0;

} // namespace

SponsorBlock::SponsorBlock(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_playback, &Playback::streamStarted, this, &SponsorBlock::onStream);
    connect(m_player, &MpvPlayer::positionChanged, this, &SponsorBlock::onPosition);
}

void SponsorBlock::onStream(const QString &videoId)
{
    m_videoId = videoId;
    m_segments.clear();
    if (!m_settings->sponsorBlock() || videoId.isEmpty())
        return;

    const QByteArray hash = QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrl url(kApi + QString::fromLatin1(hash.left(4)));
    url.setQuery(QUrlQuery{{QStringLiteral("categories"),
                            QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(kCategories))
                                                  .toJson(QJsonDocument::Compact))}});
    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId] {
        reply->deleteLater();
        // 404: no video with that hash prefix has segments
        if (videoId != m_videoId || reply->error() != QNetworkReply::NoError)
            return;
        for (const QJsonValue &video : QJsonDocument::fromJson(reply->readAll()).array()) {
            if (video[u"videoID"].toString() != videoId)
                continue;
            for (const QJsonValue &s : video[u"segments"].toArray()) {
                const QJsonArray range = s[u"segment"].toArray();
                if (s[u"actionType"].toString(QStringLiteral("skip")) != u"skip" || range.size() < 2)
                    continue;
                Segment segment{range.at(0).toDouble(), range.at(1).toDouble(), s[u"category"].toString()};
                if (segment.end - segment.start >= kMinimumSkip)
                    m_segments.append(segment);
            }
        }
        qCDebug(lcSponsor) << videoId << m_segments.size() << "segments";
    });
}

// once per segment: a seek back into one plays it
void SponsorBlock::onPosition()
{
    if (m_segments.isEmpty() || m_player->paused())
        return;
    const double position = m_player->position();
    for (Segment &segment : m_segments) {
        if (segment.done || position < segment.start || position >= segment.end - 0.5)
            continue;
        segment.done = true;
        qCDebug(lcSponsor) << "skipping" << segment.category << segment.start << "to" << segment.end;
        m_player->seek(segment.end);
        emit skipped(describe(segment.category));
        return;
    }
}

QString SponsorBlock::describe(const QString &category) const
{
    if (category == u"music_offtopic")
        return tr("Skipped a part without music");
    if (category == u"sponsor")
        return tr("Skipped a sponsor spot");
    if (category == u"selfpromo")
        return tr("Skipped self promotion");
    return tr("Skipped a reminder to subscribe");
}
