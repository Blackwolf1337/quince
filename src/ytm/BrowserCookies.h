#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QString>
#include <QStringList>

// Reads the YouTube and Google cookies of an installed browser, the session
// of a profile where the user is signed in to YouTube.
//
// Firefox family (Firefox, Zen, LibreWolf, Floorp, Waterfox): everywhere.
// Chromium family (Chrome, Chromium, Brave, Vivaldi, Edge): Linux and macOS.
// On Windows, Chrome's app-bound encryption keeps its cookies to Chrome.
//
// The newest cookie database of the browser is used, as yt-dlp does.
namespace BrowserCookies {

struct Browser
{
    QString id; // firefox, zen, chrome, ...
    QString name; // shown
};

// the supported browsers with a cookie database on this machine
QList<Browser> installed();

// Blocks (file copies, the keyring over D-Bus); run it on a worker thread.
// Empty with error when nothing could be read.
QList<QNetworkCookie> read(const QString &browserId, QString *error);

} // namespace BrowserCookies
