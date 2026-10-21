#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <cmath>

class AccountCookies;
class InnerTube;
class JsSolver;

struct Stream
{
    QString videoId;
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QString formatId;
    QString codec;
    double bitrate = 0; // kbit/s
    double duration = 0; // seconds
    QDateTime expires;
    QString client; // the InnerTube client the URL came from

    // video mode: url is the picture, audioUrl the sound (mpv joins them);
    // videoAsked without video: the track has no picture
    bool videoAsked = false;
    bool video = false;
    // AAC asked for (downloads as .m4a); never played
    bool aacAsked = false;
    QUrl audioUrl;
    int height = 0;
    // [{url, language, name, generated}] of the captions (video mode)
    QVariantList captions;

    // from YouTube Music's player answer
    double loudnessDb = NAN;
    QString trackingUrl;

    // what the player answer says about the track, used until InnerTube metadata exists
    QString title;
    QString artist;
    QString album;
    QUrl thumbnail;
};

// Turns a video id into a playable audio URL with YouTube's own player API.
//
// Two player requests go out at once:
// - WEB_REMIX (YouTube Music, with the account): loudness, the history
//   tracking URL and, for Premium, format 774 (256 kbit/s Opus). Its URLs
//   need the JS puzzles solved (JsSolver). They are used when 774 is there or
//   VISIONOS refuses the track, since for other accounts googlevideo may want
//   a PO token.
// - VISIONOS: direct URLs up to 251 (160 kbit/s Opus), no JS, no account.
class StreamResolver : public QObject
{
    Q_OBJECT

public:
    StreamResolver(InnerTube *innerTube, JsSolver *solver, QObject *parent = nullptr);
    ~StreamResolver() override;

    // signed in, WEB_REMIX requests carry the account (Premium formats)
    void setCookies(AccountCookies *cookies);

    // Emits resolved or failed later, never from inside this call. video asks
    // for picture and sound (video mode); without a picture it is sound only.
    void resolve(const QString &videoId, bool video = false);
    // sound only, AAC before Opus (downloads as .m4a)
    void resolveAac(const QString &videoId);
    bool isCached(const QString &videoId, bool video = false) const;
    // drops a URL that failed; the next resolve of it takes the other client
    void forget(const QString &videoId);
    bool isRunning(const QString &videoId, bool video = false) const;
    // highest picture of video mode (720, 1080 ...)
    void setVideoHeight(int height);

    // Video id from a watch URL, youtu.be link or a bare id; empty if none.
    static QString extractVideoId(const QString &text);

signals:
    void resolved(const Stream &stream);
    void failed(const QString &videoId, const QString &error);

private:
    struct Pending
    {
        QString videoId;
        bool video = false;
        bool aac = false;
        bool webDone = false;
        bool visionDone = false;
        bool webAllowed = false;
        bool visionSent = false;
        QString playerId; // the solved player whose signatureTimestamp the request carried
        QJsonObject web;
        QJsonObject vision;
        // age-bypass: the embedded player's answer for age-gated tracks
        bool embedSent = false;
        bool embedDone = false;
        QJsonObject embed;
        QString webError;
        QString visionError;
    };

    static QString keyOf(const QString &videoId, bool video, bool aac = false);
    void start(const QString &videoId, bool video, bool aac);
    void requestVision(const QString &key);
    void onAnswer(const QString &key);
    bool pickFormats(const QJsonObject &answer, const Pending &pending, QJsonObject *audio, QJsonObject *picture) const;
    void requestEmbedded(const QString &key);
    // URLs of a web client's answer, with the JS puzzles solved
    bool finishWeb(const QString &key, const Pending &pending, const QJsonObject &answer, const QString &client);
    bool finishVision(const QString &key, const Pending &pending);
    static Stream streamOf(const QString &videoId, const QString &client, const QJsonObject &audio,
                           const QJsonObject &picture);
    void deliver(const QString &key, Stream stream, const Pending &pending);
    void fail(const QString &videoId, const QString &error);

    InnerTube *m_innerTube = nullptr;
    JsSolver *m_solver = nullptr;
    AccountCookies *m_cookies = nullptr;
    // by key: the video id, with "#v" for video mode
    QHash<QString, Stream> m_cache;
    QHash<QString, Pending> m_pending;
    // ids whose WEB_REMIX URL failed; they take VISIONOS from now on
    QSet<QString> m_webFailed;
    int m_videoHeight = 1080;
    // answered, waiting for the first solved player
    QSet<QString> m_waiting;
};
