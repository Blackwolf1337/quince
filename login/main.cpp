// quince-login: Google sign-in for YouTube Music in a small browser window
// (the system's own web view, login/Browser.h). It writes the session cookies
// to the file named on the command line and exits with 0; closing the window
// exits with 1. The browser profile lives in memory only and is gone
// afterwards; the password never reaches Quince.
#include "Browser.h"
#include "ytm/NetscapeCookies.h"

#include <QCoreApplication>
#include <QLocale>
#include <QObject>
#include <QTextStream>
#include <QTranslator>

#include <memory>

namespace {

// YouTube Music's own "Sign in" link
const QUrl kSignInUrl(QStringLiteral(
    "https://accounts.google.com/ServiceLogin?ltmpl=music&service=youtube&passive=true"
    "&continue=https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue%26app%3Ddesktop"
    "%26next%3Dhttps%253A%252F%252Fmusic.youtube.com%252F"));

// cookies may arrive a moment after the page that sets them
constexpr int kCookieRetryMs = 500;
constexpr int kCookieRetries = 10;

void fillEmail(browser::Window &window, const QString &email)
{
    const QString fill = QStringLiteral(
        "(function(){var f=document.querySelector('input[type=email]')||document.querySelector('#identifierId');"
        " if(!f) return 'no field'; f.focus(); f.value='%1'; f.dispatchEvent(new Event('input',{bubbles:true}));"
        " var b=document.querySelector('#identifierNext button')||document.querySelector('#identifierNext');"
        " if(b) b.click(); return 'sent';})()").arg(email);
    // the field is rendered a moment after the load
    window.after(2500, [&window, fill] {
        window.runJavaScript(fill, [](const QString &r) { QTextStream(stderr) << "quince-login: probe email " << r << "\n"; });
        window.after(9000, [&window] {
            window.runJavaScript(QStringLiteral("document.body.innerText.replace(/\\s+/g,' ').slice(0,300)"),
                                 [&window](const QString &r) {
                                     QTextStream(stderr) << "quince-login: text " << r << "\n";
                                     window.quit(1);
                                 });
        });
    });
}

// Test runs: submits an address that does not exist on the email page, to see
// whether Google lets this browser past that step at all ("Couldn't find"
// yes, "This browser or app may not be secure" no); prints the page's text.
void probe(browser::Window &window, const QString &email)
{
    auto started = std::make_shared<bool>(false);
    window.pageLoaded = [&window, email, started](const QUrl &url) {
        QTextStream(stderr) << "quince-login: at " << url.host() << url.path() << "\n";
        if (*started || !url.path().contains(u"identifier"))
            return;
        *started = true;
        fillEmail(window, email);
    };
}

} // namespace

int main(int argc, char *argv[])
{
    // no event loop of its own: only for the translations
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quince-login"));
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("quince"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&translator);

    // test option --probe-email <address that does not exist>
    QStringList args = app.arguments();
    browser::Options options;
    options.title = QObject::tr("Sign in to YouTube Music");
    options.url = kSignInUrl;
    options.appId = QStringLiteral("quince");
    options.languages = QLocale::system().uiLanguages();
    QString probeEmail;
    for (int i = 1; i + 1 < args.size();) {
        if (args.at(i) == u"--probe-email") {
            probeEmail = args.at(i + 1);
            args.remove(i, 2);
        } else if (args.at(i) == u"--app-id") {
            options.appId = args.at(i + 1);
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

    return browser::run(options, [&](browser::Window &window) {
        window.closed = [&window] { window.quit(1); };
        if (!probeEmail.isEmpty()) {
            probe(window, probeEmail);
            return;
        }
        // signed in once the browser reached YouTube Music with a session cookie
        auto tries = std::make_shared<int>(0);
        auto check = std::make_shared<std::function<void()>>();
        *check = [&window, target, tries, check] {
            window.cookies([&window, target, tries, check](const QList<QNetworkCookie> &all) {
                QList<QNetworkCookie> mine;
                for (const QNetworkCookie &c : all) {
                    if (NetscapeCookies::isAccountDomain(c.domain()))
                        mine.append(c);
                }
                const QList<QNetworkCookie> account = NetscapeCookies::accountCookies(mine);
                if (NetscapeCookies::sapisid(account).isEmpty()) {
                    if (++*tries < kCookieRetries)
                        window.after(kCookieRetryMs, *check);
                    return;
                }
                QString error;
                if (!NetscapeCookies::writePrivate(target, NetscapeCookies::serialize(account), &error)) {
                    QTextStream(stderr) << "quince-login: " << error << "\n";
                    window.quit(3);
                    return;
                }
                window.quit(0);
            });
        };
        window.pageLoaded = [check, tries](const QUrl &url) {
            QTextStream(stderr) << "quince-login: at " << url.host() << url.path() << "\n";
            if (url.host() == u"music.youtube.com") {
                *tries = 0;
                (*check)();
            }
        };
    });
}
