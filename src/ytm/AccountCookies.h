#pragma once

#include <QNetworkCookieJar>
#include <QString>
#include <QTimer>

class QTemporaryFile;

// The signed-in Google session. Requests to YouTube Music take their cookies
// from here and the cookies the server renews come back here; the jar is kept
// as cookies.txt with owner-only permissions.
class AccountCookies : public QNetworkCookieJar
{
    Q_OBJECT

public:
    explicit AccountCookies(const QString &path, QObject *parent = nullptr);

    QString path() const { return m_path; }
    bool signedIn() const { return !m_sapisid.isEmpty(); }
    QString sapisid() const { return m_sapisid; }

    // reads the file again, e.g. after the login helper wrote it
    void reload();
    // takes the YouTube and Google cookies of a cookies.txt
    bool importFile(const QString &netscapePath, QString *error);
    void signOut();

    // A private copy for one yt-dlp run, so yt-dlp writing its cookies back
    // never races with us or another yt-dlp. False when signed out.
    bool writeCopy(QTemporaryFile &file) const;

    bool setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url) override;

signals:
    void signedInChanged();

private:
    void setCookies(const QList<QNetworkCookie> &cookies);
    void save();

    QString m_path;
    QString m_sapisid;
    QTimer m_saveTimer;
};
