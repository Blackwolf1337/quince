#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <functional>

class AccountCookies;
class InnerTube;
class QProcess;

// The Google account in YouTube Music: signing in and out, who is signed in,
// and what changes the account (likes, playlists, play history).
class Account : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QString name READ name NOTIFY infoChanged)
    Q_PROPERTY(QString handle READ handle NOTIFY infoChanged)
    Q_PROPERTY(QString photo READ photo NOTIFY infoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    // [{id, name}] of the browsers whose session can be taken
    Q_PROPERTY(QVariantList browsers READ browsers CONSTANT)

public:
    Account(AccountCookies *cookies, InnerTube *innerTube, QObject *parent = nullptr);
    ~Account() override;

    bool signedIn() const;
    QString name() const { return m_name; }
    QString handle() const { return m_handle; }
    QString photo() const { return m_photo; }
    bool busy() const { return m_busy; }
    // the account's own playlists, for "Add to playlist" and the sidebar
    QVariantList playlists() const { return m_playlists; }
    QVariantList browsers() const;

    // opens the quince-login window
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void importCookies(const QUrl &file);
    // the YouTube session of an installed browser (BrowserCookies)
    Q_INVOKABLE void importFromBrowser(const QString &browser);
    Q_INVOKABLE void signOut();

    // status: LIKE, DISLIKE or INDIFFERENT
    Q_INVOKABLE void rate(const QString &videoId, const QString &status);
    // the same for many tracks (album-actions), one request after the other
    Q_INVOKABLE void rateAll(const QStringList &videoIds, const QString &status);
    Q_INVOKABLE void addToPlaylist(const QString &playlistId, const QString &videoId, const QString &title);
    Q_INVOKABLE void createPlaylist(const QString &title, const QString &videoId);
    // own playlists only; setVideoId is the track's place in the playlist
    Q_INVOKABLE void removeFromPlaylist(const QString &playlistId, const QString &videoId, const QString &setVideoId);
    Q_INVOKABLE void renamePlaylist(const QString &playlistId, const QString &title);
    Q_INVOKABLE void deletePlaylist(const QString &playlistId, const QString &title);
    Q_INVOKABLE void reloadPlaylists();

    // counts a play in the account's history, like the site does; trackingUrl
    // is the videostatsPlaybackUrl of the player answer
    void reportPlay(const QString &videoId, const QString &trackingUrl);

signals:
    void signedInChanged();
    void infoChanged();
    void busyChanged();
    void playlistsChanged();
    void rated(const QString &videoId, const QString &status);
    // tracks went out of a playlist (its id as passed in)
    void playlistEdited(const QString &playlistId);
    void playlistDeleted(const QString &playlistId);
    // short feedback for the user ("Added to ...", errors)
    void message(const QString &text);

private:
    void setBusy(bool busy);
    // browse/edit_playlist; error is empty when it went through
    void editPlaylist(const QString &playlistId, const QJsonArray &actions, std::function<void(const QString &error)> done);
    void postRating(const QString &videoId, const QString &status, std::function<void(const QString &error)> done);
    void onSignedInChanged();
    void loadInfo();
    static QString helperProgram();

    AccountCookies *m_cookies = nullptr;
    InnerTube *m_innerTube = nullptr;
    QProcess *m_helper = nullptr;

    QString m_name;
    QString m_handle;
    QString m_photo;
    bool m_busy = false;
    QVariantList m_playlists;
};
