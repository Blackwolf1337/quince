#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>

class AccountCookies;
class QNetworkAccessManager;

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

    // A plain GET with the account's cookies (playback reports); status is
    // the HTTP status, 0 when the request failed.
    void ping(const QUrl &url, QObject *context, std::function<void(int status)> callback);

private:
    QJsonObject clientContext() const;
    static QString clientVersion();

    QNetworkAccessManager *m_network = nullptr;
    AccountCookies *m_cookies = nullptr;
    QString m_language;
    QString m_country;
    QByteArray m_visitorData;
};
