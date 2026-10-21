#include "ytm/InnerTube.h"
#include "ytm/AccountCookies.h"
#include "ytm/Json.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimeZone>

Q_LOGGING_CATEGORY(lcInnerTube, "quince.innertube")

namespace {

const QString kBase = QStringLiteral("https://music.youtube.com/youtubei/v1/");
const QByteArray kOrigin = QByteArrayLiteral("https://music.youtube.com");
const QByteArray kUserAgent =
    QByteArrayLiteral("Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0");
// X-YouTube-Client-Name for WEB_REMIX
const QByteArray kClientId = QByteArrayLiteral("67");

} // namespace

InnerTube::InnerTube(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    const QLocale locale = QLocale::system();
    m_language = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
    m_country = QLocale::territoryToCode(locale.territory());
    if (m_language.isEmpty())
        m_language = QStringLiteral("en");
    if (m_country.isEmpty())
        m_country = QStringLiteral("US");
}

void InnerTube::setCookies(AccountCookies *cookies)
{
    QObject *owner = cookies->parent();
    m_network->setCookieJar(cookies);
    // the network manager takes the jar; it stays with its owner instead
    cookies->setParent(owner);
    m_cookies = cookies;
}

void InnerTube::setLocale(const QString &language, const QString &country)
{
    m_language = language;
    m_country = country;
}

// WEB_REMIX accepts any recent build date; ytmusicapi uses today's date too.
QString InnerTube::clientVersion()
{
    return QStringLiteral("1.%1.01.00").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd")));
}

QJsonObject InnerTube::clientContext() const
{
    QJsonObject client{
        {QStringLiteral("clientName"), QStringLiteral("WEB_REMIX")},
        {QStringLiteral("clientVersion"), clientVersion()},
        {QStringLiteral("hl"), m_language},
        {QStringLiteral("gl"), m_country},
    };
    if (!m_visitorData.isEmpty())
        client.insert(QStringLiteral("visitorData"), QString::fromLatin1(m_visitorData));
    return QJsonObject{
        {QStringLiteral("client"), client},
        {QStringLiteral("user"), QJsonObject{}},
    };
}

void InnerTube::post(const QString &endpoint, const QJsonObject &body, QObject *context, Callback callback,
                     const QUrlQuery &query)
{
    QUrl url(kBase + endpoint);
    QUrlQuery fullQuery(query);
    fullQuery.addQueryItem(QStringLiteral("prettyPrint"), QStringLiteral("false"));
    url.setQuery(fullQuery);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setRawHeader("Origin", kOrigin);
    request.setRawHeader("X-Origin", kOrigin);
    request.setRawHeader("Referer", kOrigin + '/');
    request.setRawHeader("X-YouTube-Client-Name", kClientId);
    request.setRawHeader("X-YouTube-Client-Version", clientVersion().toLatin1());
    if (!m_visitorData.isEmpty())
        request.setRawHeader("X-Goog-Visitor-Id", m_visitorData);
    if (m_cookies && m_cookies->signedIn()) {
        // SAPISIDHASH <time>_<sha1("<time> <SAPISID> <origin>")>
        const QByteArray now = QByteArray::number(QDateTime::currentSecsSinceEpoch());
        const QByteArray hash = QCryptographicHash::hash(now + ' ' + m_cookies->sapisid().toLatin1() + ' ' + kOrigin,
                                                         QCryptographicHash::Sha1).toHex();
        request.setRawHeader("Authorization", "SAPISIDHASH " + now + '_' + hash);
        request.setRawHeader("X-Goog-AuthUser", "0");
    }

    QJsonObject fullBody(body);
    fullBody.insert(QStringLiteral("context"), clientContext());

    QNetworkReply *reply = m_network->post(request, QJsonDocument(fullBody).toJson(QJsonDocument::Compact));
    QPointer<QObject> guard(context);
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard, callback, endpoint] {
        reply->deleteLater();
        if (!guard)
            return;

        const QByteArray data = reply->readAll();
        const QJsonObject response = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            QString message = ytm::at(response, {"error", "message"}).toString();
            if (message.isEmpty())
                message = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCWarning(lcInnerTube) << endpoint << status << message;
            callback({}, status ? QStringLiteral("YouTube Music: %1 (%2)").arg(message).arg(status) : message);
            return;
        }
        if (response.isEmpty()) {
            callback({}, tr("YouTube Music sent an unreadable answer."));
            return;
        }

        const QString visitor = ytm::at(response, {"responseContext", "visitorData"}).toString();
        if (!visitor.isEmpty())
            m_visitorData = visitor.toLatin1();
        callback(response, {});
    });
}

void InnerTube::ping(const QUrl &url, QObject *context, std::function<void(int status)> callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setRawHeader("Origin", kOrigin);
    request.setRawHeader("Referer", kOrigin + '/');
    QNetworkReply *reply = m_network->get(request);
    QPointer<QObject> guard(context);
    connect(reply, &QNetworkReply::finished, this, [reply, guard, callback] {
        reply->deleteLater();
        if (guard)
            callback(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    });
}

void InnerTube::continuation(const QString &endpoint, const QJsonObject &body, const QString &token, bool command,
                             QObject *context, Callback callback)
{
    if (command) {
        post(endpoint, QJsonObject{{QStringLiteral("continuation"), token}}, context, std::move(callback));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ctoken"), token);
    query.addQueryItem(QStringLiteral("continuation"), token);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("next"));
    post(endpoint, body, context, std::move(callback), query);
}
