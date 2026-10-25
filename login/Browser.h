#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QString>
#include <QUrl>

#include <functional>

// The sign-in window's browser, in the system's own web view: WebKitGTK
// (BrowserGtk.cpp), WKWebView (BrowserMac.mm), WebView2 (BrowserWin.cpp). Its
// profile lives in memory only. All calls and callbacks on the thread that
// called run().
namespace browser {

struct Options
{
    QString title;
    QUrl url;
    // the window's app id / class (Wayland, X11)
    QString appId;
    // Accept-Language, most wanted first
    QStringList languages;
};

class Window
{
public:
    virtual ~Window() = default;

    // a page was loaded (or navigated to)
    std::function<void(const QUrl &url)> pageLoaded;
    // the user closed the window
    std::function<void()> closed;

    // all cookies of the profile
    virtual void cookies(std::function<void(const QList<QNetworkCookie> &)> done) = 0;
    // a script in the page, its result as text (test runs)
    virtual void runJavaScript(const QString &script, std::function<void(const QString &)> done) = 0;
    virtual void after(int ms, std::function<void()> callback) = 0;
    // ends run() with this exit code
    virtual void quit(int code) = 0;
};

// opens the window, calls setup with it before the page loads, and returns
// the exit code given to quit() (2 when there is no web view)
int run(const Options &options, const std::function<void(Window &)> &setup);

} // namespace browser
