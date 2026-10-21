// quince-login: Google sign-in for YouTube Music in a small browser window.
// It writes the session cookies to the file named on the command line and
// exits with 0; closing the window exits with 1. The browser profile lives in
// memory only and is gone afterwards; the password never reaches Quince.
#include "ytm/NetscapeCookies.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QHash>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QTextStream>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace {

// YouTube Music's own "Sign in" link
const QUrl kSignInUrl(QStringLiteral(
    "https://accounts.google.com/ServiceLogin?ltmpl=music&service=youtube&passive=true"
    "&continue=https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue%26app%3Ddesktop"
    "%26next%3Dhttps%253A%252F%252Fmusic.youtube.com%252F"));

QString cookieKey(const QNetworkCookie &c)
{
    return c.domain() + u'|' + c.path() + u'|' + QString::fromLatin1(c.name());
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("quince-login"));
    QApplication::setApplicationDisplayName(QStringLiteral("Quince"));
    QApplication::setDesktopFileName(QStringLiteral("quince"));

    // Quince's own texts and Qt's standard ones (dialog buttons) in the system language
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("quince"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&translator);
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator);
    // closing the window without signing in must not end with exit code 0
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "usage: quince-login <cookies.txt to write>\n";
        return 2;
    }
    const QString target = args.at(1);

    // no storage name: an off-the-record profile kept in memory only
    auto *profile = new QWebEngineProfile(&app);
    // Google refuses sign-ins from browsers it does not know; look like Chrome
    QString agent = profile->httpUserAgent();
    agent.remove(QRegularExpression(QStringLiteral(" QtWebEngine/[^ ]+")));
    profile->setHttpUserAgent(agent);

    QHash<QString, QNetworkCookie> cookies;
    QWebEngineCookieStore *store = profile->cookieStore();
    QObject::connect(store, &QWebEngineCookieStore::cookieAdded, &app, [&cookies](const QNetworkCookie &c) {
        if (NetscapeCookies::isAccountDomain(c.domain()))
            cookies.insert(cookieKey(c), c);
    });
    QObject::connect(store, &QWebEngineCookieStore::cookieRemoved, &app, [&cookies](const QNetworkCookie &c) {
        cookies.remove(cookieKey(c));
    });
    store->loadAllCookies();

    QWebEngineView view;
    auto *page = new QWebEnginePage(profile, &view);
    view.setPage(page);
    view.setWindowTitle(QObject::tr("Sign in to YouTube Music"));
    view.resize(520, 720);

    bool done = false;
    auto finish = [&] {
        if (done)
            return;
        const QList<QNetworkCookie> account = NetscapeCookies::accountCookies(cookies.values());
        if (NetscapeCookies::sapisid(account).isEmpty())
            return;
        QString error;
        if (!NetscapeCookies::writePrivate(target, NetscapeCookies::serialize(account), &error)) {
            QTextStream(stderr) << "quince-login: " << error << "\n";
            QCoreApplication::exit(3);
            return;
        }
        done = true;
        QCoreApplication::exit(0);
    };

    // signed in once the browser reached YouTube Music with a session cookie;
    // the cookies may arrive a moment after the page, so check on both
    bool reachedMusic = false;
    QObject::connect(page, &QWebEnginePage::urlChanged, &app, [&](const QUrl &url) {
        if (url.host() == u"music.youtube.com") {
            reachedMusic = true;
            finish();
        }
    });
    QObject::connect(store, &QWebEngineCookieStore::cookieAdded, &app, [&] {
        if (reachedMusic)
            finish();
    });

    view.load(kSignInUrl);
    view.show();

    QObject::connect(&app, &QApplication::lastWindowClosed, &app, [&] {
        if (!done)
            QCoreApplication::exit(1);
    });
    return app.exec();
}
