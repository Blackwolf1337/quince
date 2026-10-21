#include "ytm/Account.h"
#include "ytm/AccountCookies.h"
#include "ytm/BrowserCookies.h"
#include "ytm/InnerTube.h"
#include "ytm/Json.h"
#include "ytm/Parser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QPointer>
#include <QThread>
#include <QLoggingCategory>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrlQuery>

#include <memory>

Q_LOGGING_CATEGORY(lcAccount, "quince.account")

namespace {

QString homeCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/home.json");
}

// client playback nonce of a playback report, as the site makes it
QString playbackNonce()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    QString cpn;
    for (int i = 0; i < 16; ++i)
        cpn += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(64)]);
    return cpn;
}

} // namespace

Account::Account(AccountCookies *cookies, InnerTube *innerTube, QObject *parent)
    : QObject(parent)
    , m_cookies(cookies)
    , m_innerTube(innerTube)
{
    connect(m_cookies, &AccountCookies::signedInChanged, this, &Account::onSignedInChanged);
    if (signedIn()) {
        loadInfo();
        reloadPlaylists();
    }
}

Account::~Account()
{
    if (m_helper) {
        m_helper->disconnect(this);
        m_helper->kill();
    }
}

bool Account::signedIn() const
{
    return m_cookies->signedIn();
}

void Account::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Account::onSignedInChanged()
{
    // the cached home page belongs to the account it was made for
    QFile::remove(homeCachePath());
    m_name.clear();
    m_handle.clear();
    m_photo.clear();
    m_playlists.clear();
    emit infoChanged();
    emit playlistsChanged();
    if (signedIn()) {
        loadInfo();
        reloadPlaylists();
    }
    emit signedInChanged();
}

QString Account::helperProgram()
{
    const QString name = QStringLiteral("quince-login");
    const QString local = QStandardPaths::findExecutable(name, {QCoreApplication::applicationDirPath()});
    return local.isEmpty() ? QStandardPaths::findExecutable(name) : local;
}

void Account::signIn()
{
    if (m_helper)
        return;
    const QString program = helperProgram();
    if (program.isEmpty()) {
        emit message(tr("The sign-in window (quince-login) is not installed. Import a cookies.txt instead."));
        return;
    }

    m_helper = new QProcess(this);
    m_helper->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(m_helper, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_helper->deleteLater();
        m_helper = nullptr;
        setBusy(false);
        if (status == QProcess::NormalExit && code == 0) {
            m_cookies->reload();
            if (!signedIn())
                emit message(tr("The sign-in did not give a YouTube session."));
        }
    });
    connect(m_helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        emit message(tr("The sign-in window could not be started: %1").arg(m_helper->errorString()));
        m_helper->deleteLater();
        m_helper = nullptr;
        setBusy(false);
    });
    setBusy(true);
    m_helper->start(program, {m_cookies->path()});
}

void Account::importCookies(const QUrl &file)
{
    QString error;
    if (!m_cookies->importFile(file.toLocalFile(), &error))
        emit message(tr("Import failed: %1").arg(error));
}

QVariantList Account::browsers() const
{
    QVariantList out;
    for (const BrowserCookies::Browser &browser : BrowserCookies::installed())
        out.append(QVariantMap{{QStringLiteral("id"), browser.id}, {QStringLiteral("name"), browser.name}});
    return out;
}

void Account::importFromBrowser(const QString &browser)
{
    if (m_busy)
        return;
    setBusy(true);
    // file copies and the keyring over D-Bus, off the UI thread
    QPointer<Account> self(this);
    QThread *thread = QThread::create([self, browser] {
        QString error;
        const QList<QNetworkCookie> cookies = BrowserCookies::read(browser, &error);
        QMetaObject::invokeMethod(qApp, [self, cookies, error] {
            if (!self)
                return;
            self->setBusy(false);
            QString importError = error;
            if (importError.isEmpty())
                self->m_cookies->importCookies(cookies, &importError);
            if (!importError.isEmpty())
                emit self->message(tr("No YouTube session found: %1").arg(importError));
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Account::signOut()
{
    m_cookies->signOut();
}

void Account::loadInfo()
{
    m_innerTube->post(QStringLiteral("account/account_menu"), {}, this,
                      [this](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty()) {
            qCWarning(lcAccount) << "account info:" << error;
            return;
        }
        const QJsonValue header = ytm::at(response, {"actions", 0, "openPopupAction", "popup", "multiPageMenuRenderer",
                                                     "header", "activeAccountHeaderRenderer"});
        // YouTube answers a dead session like a guest; only the account menu tells
        if (!header.isObject()) {
            emit message(tr("The saved YouTube session is no longer valid. Please sign in again."));
            return;
        }
        m_name = ytm::textOf(ytm::at(header, {"accountName"}));
        m_handle = ytm::textOf(ytm::at(header, {"channelHandle"}));
        m_photo = ytm::at(header, {"accountPhoto", "thumbnails", 0, "url"}).toString();
        emit infoChanged();
    });
}

void Account::reloadPlaylists()
{
    if (!signedIn())
        return;
    m_innerTube->post(QStringLiteral("browse"), {{QStringLiteral("browseId"), QStringLiteral("FEmusic_liked_playlists")}},
                      this, [this](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty()) {
            qCWarning(lcAccount) << "playlists:" << error;
            return;
        }
        const ytm::Page page = ytm::parseBrowse(response, QStringLiteral("FEmusic_liked_playlists"));
        QVariantList playlists;
        for (const QVariant &shelf : page.shelves) {
            for (const QVariant &v : shelf.toMap().value(QStringLiteral("items")).toList()) {
                const QVariantMap item = v.toMap();
                if (item.value(QStringLiteral("kind")).toString() == u"playlist")
                    playlists.append(item);
            }
        }
        m_playlists = playlists;
        emit playlistsChanged();
    });
}

void Account::rate(const QString &videoId, const QString &status)
{
    if (!signedIn() || videoId.isEmpty())
        return;
    QString endpoint = QStringLiteral("like/removelike");
    if (status == u"LIKE")
        endpoint = QStringLiteral("like/like");
    else if (status == u"DISLIKE")
        endpoint = QStringLiteral("like/dislike");

    m_innerTube->post(endpoint, {{QStringLiteral("target"), QJsonObject{{QStringLiteral("videoId"), videoId}}}}, this,
                      [this, videoId, status](const QJsonObject &, const QString &error) {
        if (!error.isEmpty()) {
            emit message(tr("Rating failed: %1").arg(error));
            return;
        }
        emit rated(videoId, status);
    });
}

void Account::addToPlaylist(const QString &playlistId, const QString &videoId, const QString &title)
{
    const QString id = playlistId.startsWith(u"VL") ? playlistId.mid(2) : playlistId;
    const QJsonObject body{
        {QStringLiteral("playlistId"), id},
        {QStringLiteral("actions"), QJsonArray{QJsonObject{
             {QStringLiteral("action"), QStringLiteral("ACTION_ADD_VIDEO")},
             {QStringLiteral("addedVideoId"), videoId},
         }}},
    };
    m_innerTube->post(QStringLiteral("browse/edit_playlist"), body, this,
                      [this, title](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty() || !response.value(u"status").toString().contains(u"SUCCEEDED")) {
            emit message(tr("Could not add to \"%1\". %2").arg(title, error));
            return;
        }
        emit message(tr("Added to \"%1\"").arg(title));
    });
}

void Account::createPlaylist(const QString &title, const QString &videoId)
{
    QJsonObject body{
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), QString()},
        {QStringLiteral("privacyStatus"), QStringLiteral("PRIVATE")},
    };
    if (!videoId.isEmpty())
        body.insert(QStringLiteral("videoIds"), QJsonArray{videoId});
    m_innerTube->post(QStringLiteral("playlist/create"), body, this,
                      [this, title](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty() || !response.contains(u"playlistId")) {
            emit message(tr("Could not create \"%1\". %2").arg(title, error));
            return;
        }
        emit message(tr("Created \"%1\"").arg(title));
        reloadPlaylists();
    });
}

void Account::reportPlay(const QString &videoId, const QString &trackingUrl)
{
    if (!signedIn() || trackingUrl.isEmpty())
        return;
    QUrl url(trackingUrl);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("ver"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("c"), QStringLiteral("WEB_REMIX"));
    query.addQueryItem(QStringLiteral("cpn"), playbackNonce());
    url.setQuery(query);
    m_innerTube->ping(url, this, [videoId](int status) {
        qCDebug(lcAccount) << "play of" << videoId << "reported:" << status;
    });
}
