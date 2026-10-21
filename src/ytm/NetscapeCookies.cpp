#include "ytm/NetscapeCookies.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>

namespace NetscapeCookies {

namespace {

const QByteArray kHttpOnlyPrefix = QByteArrayLiteral("#HttpOnly_");

} // namespace

QList<QNetworkCookie> parse(const QByteArray &data)
{
    QList<QNetworkCookie> cookies;
    for (QByteArray line : data.split('\n')) {
        line = line.trimmed();
        bool httpOnly = false;
        if (line.startsWith(kHttpOnlyPrefix)) {
            line = line.mid(kHttpOnlyPrefix.size());
            httpOnly = true;
        } else if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        // domain, subdomains, path, secure, expires, name, value
        const QList<QByteArray> f = line.split('\t');
        if (f.size() < 7)
            continue;
        QNetworkCookie cookie(f[5], f[6]);
        QString domain = QString::fromLatin1(f[0]);
        if (f[1] == "TRUE" && !domain.startsWith(u'.'))
            domain.prepend(u'.');
        cookie.setDomain(domain);
        cookie.setPath(QString::fromLatin1(f[2]));
        cookie.setSecure(f[3] == "TRUE");
        cookie.setHttpOnly(httpOnly);
        const qint64 expires = f[4].toLongLong();
        if (expires > 0)
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expires, QTimeZone::UTC));
        cookies.append(cookie);
    }
    return cookies;
}

QByteArray serialize(const QList<QNetworkCookie> &cookies)
{
    QByteArray out = QByteArrayLiteral("# Netscape HTTP Cookie File\n# written by Quince, do not share\n\n");
    for (const QNetworkCookie &c : cookies) {
        const QString domain = c.domain();
        if (c.isHttpOnly())
            out += kHttpOnlyPrefix;
        out += domain.toLatin1() + '\t';
        out += domain.startsWith(u'.') ? "TRUE\t" : "FALSE\t";
        out += (c.path().isEmpty() ? QByteArrayLiteral("/") : c.path().toLatin1()) + '\t';
        out += c.isSecure() ? "TRUE\t" : "FALSE\t";
        out += QByteArray::number(c.isSessionCookie() ? 0 : c.expirationDate().toSecsSinceEpoch()) + '\t';
        out += c.name() + '\t' + c.value() + '\n';
    }
    return out;
}

bool isAccountDomain(const QString &domain)
{
    QString d = domain;
    if (d.startsWith(u'.'))
        d.remove(0, 1);
    return d == u"youtube.com" || d.endsWith(u".youtube.com") || d == u"google.com" || d.endsWith(u".google.com");
}

QList<QNetworkCookie> accountCookies(const QList<QNetworkCookie> &cookies)
{
    QList<QNetworkCookie> out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QNetworkCookie &c : cookies) {
        if (!isAccountDomain(c.domain()))
            continue;
        if (!c.isSessionCookie() && c.expirationDate() < now)
            continue;
        out.append(c);
    }
    return out;
}

QString sapisid(const QList<QNetworkCookie> &cookies)
{
    QString fallback;
    for (const QNetworkCookie &c : cookies) {
        if (!c.domain().endsWith(u"youtube.com"))
            continue;
        if (c.name() == "__Secure-3PAPISID")
            return QString::fromLatin1(c.value());
        if (c.name() == "SAPISID")
            fallback = QString::fromLatin1(c.value());
    }
    return fallback;
}

bool writePrivate(const QString &path, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(data);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace NetscapeCookies
