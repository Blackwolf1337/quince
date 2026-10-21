#include "ytm/AccountCookies.h"
#include "ytm/NetscapeCookies.h"

#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCookies, "quince.cookies")

namespace {

// renewed cookies are written out this long after the last change
constexpr int kSaveDelayMs = 3000;

} // namespace

AccountCookies::AccountCookies(const QString &path, QObject *parent)
    : QNetworkCookieJar(parent)
    , m_path(path)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &AccountCookies::save);
    reload();
}

void AccountCookies::setCookies(const QList<QNetworkCookie> &cookies)
{
    const bool was = signedIn();
    setAllCookies(NetscapeCookies::accountCookies(cookies));
    m_sapisid = NetscapeCookies::sapisid(allCookies());
    if (was != signedIn())
        emit signedInChanged();
}

void AccountCookies::reload()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setCookies({});
        return;
    }
    setCookies(NetscapeCookies::parse(file.readAll()));
    qCDebug(lcCookies) << "loaded" << allCookies().size() << "cookies, signed in:" << signedIn();
}

bool AccountCookies::importFile(const QString &netscapePath, QString *error)
{
    QFile file(netscapePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return importCookies(NetscapeCookies::parse(file.readAll()), error);
}

bool AccountCookies::importCookies(const QList<QNetworkCookie> &all, QString *error)
{
    const QList<QNetworkCookie> cookies = NetscapeCookies::accountCookies(all);
    if (NetscapeCookies::sapisid(cookies).isEmpty()) {
        if (error)
            *error = tr("These cookies are not from a signed-in YouTube session.");
        return false;
    }
    if (!NetscapeCookies::writePrivate(m_path, NetscapeCookies::serialize(cookies), error))
        return false;
    setCookies(cookies);
    return true;
}

void AccountCookies::signOut()
{
    m_saveTimer.stop();
    QFile::remove(m_path);
    setCookies({});
}

bool AccountCookies::setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url)
{
    // anonymous answers set cookies too (visitor id); only a session is kept on disk
    const bool changed = QNetworkCookieJar::setCookiesFromUrl(cookies, url);
    if (changed && signedIn())
        m_saveTimer.start();
    return changed;
}

void AccountCookies::save()
{
    if (!signedIn())
        return;
    QString error;
    if (!NetscapeCookies::writePrivate(m_path, NetscapeCookies::serialize(NetscapeCookies::accountCookies(allCookies())),
                                       &error))
        qCWarning(lcCookies) << "could not save cookies:" << error;
}
