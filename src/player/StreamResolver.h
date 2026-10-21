#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

class AccountCookies;
class QProcess;

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

    // what yt-dlp reports about the track, used until InnerTube metadata exists
    QString title;
    QString artist;
    QString album;
    QUrl thumbnail;
};

// Turns a video id into a playable audio URL by running yt-dlp. yt-dlp (with
// deno for YouTube's JS challenges) is kept external so YouTube player changes
// are fixed by updating yt-dlp, not this program.
class StreamResolver : public QObject
{
    Q_OBJECT

public:
    explicit StreamResolver(QObject *parent = nullptr);
    ~StreamResolver() override;

    // signed in, yt-dlp gets the account's cookies (Premium formats)
    void setCookies(AccountCookies *cookies);

    // Emits resolved or failed later, never from inside this call.
    void resolve(const QString &videoId);
    bool isCached(const QString &videoId) const;
    // drops a URL that failed, so the next resolve asks yt-dlp again
    void forget(const QString &videoId);
    bool isRunning(const QString &videoId) const;

    // Video id from a watch URL, youtu.be link or a bare id; empty if none.
    static QString extractVideoId(const QString &text);

signals:
    void resolved(const Stream &stream);
    void failed(const QString &videoId, const QString &error);

private:
    void onFinished(QProcess *process, const QString &videoId);
    void fail(const QString &videoId, const QString &error);
    static QString program();

    AccountCookies *m_cookies = nullptr;
    QHash<QString, Stream> m_cache;
    QHash<QString, QProcess *> m_running;
};
