#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>

class AccountCookies;
class QNetworkAccessManager;
class QNetworkReply;

// Another InnerTube client (www.youtube.com, no account), for the player
// answers YouTube only gives to apps.
struct ForeignClient
{
    QJsonObject client; // context.client
    QByteArray nameId; // X-YouTube-Client-Name
    QByteArray userAgent;
};

// Client for YouTube Music's internal API (youtubei/v1, client WEB_REMIX).
class InnerTube : public QObject
{
    Q_OBJECT

public:
    // error is empty on success
    using Callback = std::function<void(const QJsonObject &response, const QString &error)>;

    explicit InnerTube(QObject *parent = nullptr);

    void setLocale(const QString &language, const QString &country);
    // requests carry the account's cookies and the SAPISIDHASH header from now on
    void setCookies(AccountCookies *cookies);

    // POSTs body plus the client context to youtubei/v1/<endpoint>. The
    // callback runs on the main thread, and never if context is destroyed.
    void post(const QString &endpoint, const QJsonObject &body, QObject *context, Callback callback,
              const QUrlQuery &query = {});

    // Next page of a browse or search answer. Old style tokens (from
    // nextContinuationData) go into the URL with the original body, new style
    // tokens (continuationCommand) replace the body.
    void continuation(const QString &endpoint, const QJsonObject &body, const QString &token, bool command,
                      QObject *context, Callback callback);

    // The same as post, as client on www.youtube.com and without the account.
    // It carries the visitor id of the own answers; without one YouTube
    // answers app clients with its bot check.
    void postAs(const ForeignClient &client, const QString &endpoint, const QJsonObject &body, QObject *context,
                Callback callback);

    // A plain GET without the account (player JS); error is empty on success.
    void get(const QUrl &url, QObject *context, std::function<void(const QByteArray &data, const QString &error)> callback);

    // a visitor id came with an answer to post
    bool hasVisitorData() const { return !m_visitorData.isEmpty(); }

    // what WEB_REMIX requests and its streams send as User-Agent
    static QByteArray userAgent();

    // A plain GET with the account's cookies (playback reports); status is
    // the HTTP status, 0 when the request failed.
    void ping(const QUrl &url, QObject *context, std::function<void(int status)> callback);

private:
    QJsonObject clientContext() const;
    void finish(QNetworkReply *reply, QObject *context, Callback callback, const QString &endpoint, bool own);
    static QString clientVersion();

    QNetworkAccessManager *m_network = nullptr;
    // without the cookie jar
    QNetworkAccessManager *m_plain = nullptr;
    AccountCookies *m_cookies = nullptr;
    QString m_language;
    QString m_country;
    QByteArray m_visitorData;
};
