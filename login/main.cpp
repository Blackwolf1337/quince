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
#include <QTextStream>
#include <QTimer>
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

    // test options: --app-id <id> (window class), --screenshot <base> (pictures
    // of the page), --probe-email <address that does not exist> (is this
    // browser let past the email step: "Couldn't find" yes, "rejected" no)
    QStringList args = app.arguments();
    QString screenshotBase;
    QString probeEmail;
    for (int i = 1; i + 1 < args.size();) {
        if (args.at(i) == u"--probe-email") {
            probeEmail = args.at(i + 1);
            args.remove(i, 2);
            continue;
        }
        if (args.at(i) == u"--app-id") {
            QApplication::setDesktopFileName(args.at(i + 1));
            args.remove(i, 2);
        } else if (args.at(i) == u"--screenshot") {
            screenshotBase = args.at(i + 1);
            args.remove(i, 2);
        } else {
            ++i;
        }
    }
    if (args.size() < 2) {
        QTextStream(stderr) << "usage: quince-login <cookies.txt to write>\n";
        return 2;
    }
    const QString target = args.at(1);

    // no storage name: an off-the-record profile kept in memory only
    auto *profile = new QWebEngineProfile(&app);
    // The user agent stays QtWebEngine's own: Google rejects a browser that
    // claims to be Chrome or Firefox without being it ("This browser or app
    // may not be secure"), an honest embedded Chromium may sign in (tested
    // 2026-10-21 with --probe-email).
    profile->setHttpAcceptLanguage(QLocale::system().uiLanguages().join(u','));

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

    QObject::connect(page, &QWebEnginePage::urlChanged, &app, [](const QUrl &url) {
        QTextStream(stderr) << "quince-login: at " << url.host() << url.path() << "\n";
    });
    QObject::connect(page, &QWebEnginePage::loadFinished, &app, [](bool ok) {
        QTextStream(stderr) << "quince-login: page loaded " << (ok ? "ok" : "with errors") << "\n";
    });
    // test runs: submit an address that does not exist, to see whether Google
    // lets this browser past the email step at all
    QObject::connect(page, &QWebEnginePage::loadFinished, &app, [page, probeEmail] {
        if (probeEmail.isEmpty())
            return;
        static bool started = false;
        if (started || !page->url().path().contains(u"identifier"))
            return;
        started = true;
        // the field is rendered a moment after the load; try once a second
        auto *retry = new QTimer(page);
        QObject::connect(retry, &QTimer::timeout, page, [page, probeEmail, retry] {
            page->runJavaScript(QStringLiteral(
                "(function(){var f=document.querySelector('input[type=email]')||document.querySelector('#identifierId');"
                " if(!f) return 'no field: '+Array.from(document.querySelectorAll('input')).map(function(i){return i.type+'#'+i.id+'/'+i.name;}).join(' ');"
                " f.focus(); f.value='%1'; f.dispatchEvent(new Event('input',{bubbles:true}));"
                " var b=document.querySelector('#identifierNext button')||document.querySelector('#identifierNext');"
                " if(b) b.click(); return 'sent';})()").arg(probeEmail),
                [retry](const QVariant &r) {
                    QTextStream(stderr) << "quince-login: probe email " << r.toString() << "\n";
                    if (r.toString() == u"sent")
                        retry->stop();
                });
        });
        retry->start(1000);
        QTimer::singleShot(9000, page, [page] {
            page->runJavaScript(QStringLiteral("document.body.innerText.replace(/\\s+/g,' ').slice(0,300)"),
                                [](const QVariant &r) { QTextStream(stderr) << "quince-login: text " << r.toString() << "\n"; });
        });
    });
    if (!screenshotBase.isEmpty()) {
        auto *shots = new QTimer(&app);
        QObject::connect(shots, &QTimer::timeout, &app, [&view, screenshotBase] {
            static int n = 0;
            view.grab().save(QStringLiteral("%1-%2.png").arg(screenshotBase).arg(n++));
        });
        shots->start(4000);
    }

    view.load(kSignInUrl);
    view.show();

    QObject::connect(&app, &QApplication::lastWindowClosed, &app, [&] {
        if (!done)
            QCoreApplication::exit(1);
    });
    return app.exec();
}
