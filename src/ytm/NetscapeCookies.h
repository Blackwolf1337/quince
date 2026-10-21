#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkCookie>
#include <QString>

// cookies.txt in the Netscape format curl and yt-dlp read. Shared by the app
// and the quince-login helper.
namespace NetscapeCookies {

QList<QNetworkCookie> parse(const QByteArray &data);
QByteArray serialize(const QList<QNetworkCookie> &cookies);

// only what YouTube Music and its Google sign-in need
QList<QNetworkCookie> accountCookies(const QList<QNetworkCookie> &cookies);
bool isAccountDomain(const QString &domain);

// the cookie the SAPISIDHASH header is computed from; empty when signed out
QString sapisid(const QList<QNetworkCookie> &cookies);

// writes data to path with owner-only permissions
bool writePrivate(const QString &path, const QByteArray &data, QString *error = nullptr);

} // namespace NetscapeCookies
