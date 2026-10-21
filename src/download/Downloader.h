#pragma once

#include "download/AudioFile.h"
#include "player/StreamResolver.h"

#include <QAbstractListModel>
#include <QFile>
#include <QList>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class InnerTube;
class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// downloader: tracks, albums and playlists as audio files with tags and
// cover, into the music folder. Two downloads run at a time; the list is the
// Downloads page.
class Downloader : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The downloader is owned by App")

    Q_PROPERTY(int running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        ThumbnailRole,
        StateRole, // waiting, resolving, downloading, writing, done, failed
        ProgressRole, // 0..1
        PathRole,
        ErrorRole,
    };

    Downloader(StreamResolver *resolver, InnerTube *innerTube, Settings *settings, QObject *parent = nullptr);
    ~Downloader() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int running() const;
    QString folder() const;

    // a track, or every track of an album or playlist (item with browseId)
    Q_INVOKABLE void download(const QVariantMap &item);
    Q_INVOKABLE void retry(int row);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void openFolder(int row = -1);

signals:
    void runningChanged();
    void folderChanged();
    // short feedback ("Downloading 12 tracks of ...")
    void message(const QString &text);

private:
    struct Job
    {
        QVariantMap item;
        AudioFile::Tags tags;
        QString coverUrl;
        QString state = QStringLiteral("waiting");
        double progress = 0;
        QString path;
        QString error;
        AudioFile::Format format = AudioFile::Format::Opus;
        int mp3Kbps = 320;
        // while downloading
        Stream stream;
        qint64 size = 0;
        qint64 done = 0;
        std::shared_ptr<QFile> part;
        QPointer<QNetworkReply> reply;
        bool coverDone = false;
        bool streamDone = false;
    };

    void addJob(const QVariantMap &track, const AudioFile::Tags &albumTags, const QString &coverUrl, int number);
    void addPage(const QString &browseId);
    void schedule();
    void startJob(int row);
    void onResolved(const Stream &stream);
    void onFailed(const QString &videoId, const QString &error);
    void fetchChunk(int row);
    void fetchCover(int row);
    void maybeWrite(int row);
    void finish(int row, const QString &error);
    void changed(int row);
    int rowOf(const Job *job) const;
    QString targetPath(const Job &job) const;
    AudioFile::Format format() const;

    StreamResolver *m_resolver = nullptr;
    InnerTube *m_innerTube = nullptr;
    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QList<std::shared_ptr<Job>> m_jobs;
};
