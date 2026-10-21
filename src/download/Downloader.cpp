#include "download/Downloader.h"
#include "app/Settings.h"
#include "ytm/InnerTube.h"
#include "ytm/Parser.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcDownload, "quince.download")

namespace {

// at most this many jobs resolve, download or write at once
constexpr int kParallel = 2;
// googlevideo serves bounded ranges best (as yt-dlp asks for them)
constexpr qint64 kChunk = 10 * 1024 * 1024;
// pages of a long playlist fetched at most
constexpr int kMaxPages = 60;

QString artistsOf(const QVariantMap &item)
{
    QStringList names;
    for (const QVariant &a : item.value(QStringLiteral("artists")).toList())
        names.append(a.toMap().value(QStringLiteral("name")).toString());
    if (names.isEmpty())
        return item.value(QStringLiteral("artistText")).toString();
    return names.join(u", ");
}

// a file or folder name: no path separators or characters Windows refuses
QString safeName(QString name)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(bad, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(u'.'))
        name.chop(1);
    if (name.size() > 120)
        name = name.left(120).trimmed();
    return name.isEmpty() ? QStringLiteral("_") : name;
}

QString yearOf(const QString &text)
{
    static const QRegularExpression year(QStringLiteral("\\b(19|20)\\d{2}\\b"));
    return year.match(text).captured(0);
}

} // namespace

Downloader::Downloader(StreamResolver *resolver, InnerTube *innerTube, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_resolver(resolver)
    , m_innerTube(innerTube)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_resolver, &StreamResolver::resolved, this, &Downloader::onResolved);
    connect(m_resolver, &StreamResolver::failed, this, &Downloader::onFailed);
    connect(m_settings, &Settings::changed, this, &Downloader::folderChanged);
}

Downloader::~Downloader()
{
    for (const auto &job : std::as_const(m_jobs)) {
        if (job->reply)
            job->reply->abort();
        if (job->part)
            job->part->remove();
    }
}

int Downloader::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_jobs.size());
}

QVariant Downloader::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_jobs.size())
        return {};
    const Job &job = *m_jobs.at(index.row());
    switch (role) {
    case TitleRole:
        return job.tags.title;
    case ArtistRole:
        return job.tags.artist;
    case AlbumRole:
        return job.tags.album;
    case ThumbnailRole:
        return job.item.value(QStringLiteral("thumbnail"));
    case StateRole:
        return job.state;
    case ProgressRole:
        return job.progress;
    case PathRole:
        return job.path;
    case ErrorRole:
        return job.error;
    }
    return {};
}

QHash<int, QByteArray> Downloader::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {ThumbnailRole, "thumbnail"},
        {StateRole, "state"},
        {ProgressRole, "progress"},
        {PathRole, "path"},
        {ErrorRole, "error"},
    };
}

int Downloader::running() const
{
    int count = 0;
    for (const auto &job : m_jobs) {
        if (job->state == u"resolving" || job->state == u"downloading" || job->state == u"writing")
            ++count;
    }
    return count;
}

QString Downloader::folder() const
{
    return m_settings->downloadFolder();
}

AudioFile::Format Downloader::format() const
{
    const QString name = m_settings->downloadFormat();
    if (name == u"m4a")
        return AudioFile::Format::M4a;
    if (name == u"mp3")
        return AudioFile::Format::Mp3;
    return AudioFile::Format::Opus;
}

void Downloader::download(const QVariantMap &item)
{
    if (!item.value(QStringLiteral("videoId")).toString().isEmpty()) {
        AudioFile::Tags none;
        addJob(item, none, item.value(QStringLiteral("thumbnail")).toString(), 0);
        emit message(tr("Downloading \"%1\".").arg(item.value(QStringLiteral("title")).toString()));
        return;
    }
    const QString browseId = item.value(QStringLiteral("browseId")).toString();
    if (!browseId.isEmpty())
        addPage(browseId);
}

// an album or playlist page, every page of it, as jobs
void Downloader::addPage(const QString &browseId)
{
    const QJsonObject body{{QStringLiteral("browseId"), browseId}};
    m_innerTube->post(QStringLiteral("browse"), body, this,
                      [this, browseId, body](const QJsonObject &response, const QString &error) {
        if (!error.isEmpty()) {
            emit message(tr("Download failed: %1").arg(error));
            return;
        }
        const ytm::Page page = ytm::parseBrowse(response, browseId);
        const QVariantMap header = page.header;
        const bool album = header.value(QStringLiteral("kind")).toString() == u"album" || browseId.startsWith(u"MPRE");

        AudioFile::Tags albumTags;
        QString cover;
        if (album) {
            albumTags.album = header.value(QStringLiteral("title")).toString();
            albumTags.albumArtist = artistsOf(header);
            if (albumTags.albumArtist.isEmpty())
                albumTags.albumArtist = header.value(QStringLiteral("strapline")).toString();
            albumTags.year = yearOf(header.value(QStringLiteral("subtitle")).toString());
            cover = header.value(QStringLiteral("thumbnail")).toString();
        }
        const QString name = header.value(QStringLiteral("title")).toString();

        auto added = std::make_shared<int>(0);
        const int limit = album ? 0 : m_settings->downloadPlaylistMax();
        auto addItems = [this, albumTags, cover, added, limit](const QVariantList &items) {
            for (const QVariant &v : items) {
                const QVariantMap track = v.toMap();
                if (track.value(QStringLiteral("videoId")).toString().isEmpty()
                    || track.value(QStringLiteral("unavailable")).toBool())
                    continue;
                if (limit > 0 && *added >= limit)
                    return;
                ++*added;
                const int number = albumTags.album.isEmpty() ? 0
                    : (track.value(QStringLiteral("index")).toInt() > 0 ? track.value(QStringLiteral("index")).toInt()
                                                                        : *added);
                addJob(track, albumTags, cover.isEmpty() ? track.value(QStringLiteral("thumbnail")).toString() : cover,
                       number);
            }
        };
        for (const QVariant &shelf : page.shelves) {
            const QVariantMap s = shelf.toMap();
            if (s.value(QStringLiteral("layout")).toString() == u"list")
                addItems(s.value(QStringLiteral("items")).toList());
        }

        // a long playlist goes on in further pages
        auto next = std::make_shared<std::function<void(QString, bool, int)>>();
        *next = [this, body, addItems, added, limit, name, weak = std::weak_ptr(next)](QString token, bool command,
                                                                                      int pages) {
            if (token.isEmpty() || pages >= kMaxPages || (limit > 0 && *added >= limit)) {
                emit message(tr("Downloading %n track(s) of \"%1\".", "", *added).arg(name));
                return;
            }
            m_innerTube->continuation(QStringLiteral("browse"), body, token, command, this,
                                      [addItems, pages, next = weak.lock()](const QJsonObject &response,
                                                                             const QString &error) {
                if (!error.isEmpty() || !next)
                    return;
                const ytm::Page more = ytm::parseContinuation(response);
                addItems(more.items);
                (*next)(more.continuation, more.continuationCommand, pages + 1);
            });
        };
        QString token;
        bool command = page.continuationCommand;
        for (const QVariant &shelf : page.shelves) {
            const QVariantMap s = shelf.toMap();
            if (s.value(QStringLiteral("layout")).toString() == u"list" && !s.value(QStringLiteral("continuation")).toString().isEmpty()) {
                token = s.value(QStringLiteral("continuation")).toString();
                command = s.value(QStringLiteral("continuationCommand")).toBool();
            }
        }
        if (token.isEmpty())
            token = page.continuation;
        (*next)(token, command, 0);
    });
}

void Downloader::addJob(const QVariantMap &track, const AudioFile::Tags &albumTags, const QString &coverUrl, int number)
{
    auto job = std::make_shared<Job>();
    job->item = track;
    job->tags = albumTags;
    job->tags.title = track.value(QStringLiteral("title")).toString();
    job->tags.artist = artistsOf(track);
    if (job->tags.album.isEmpty())
        job->tags.album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    if (job->tags.year.isEmpty())
        job->tags.year = track.value(QStringLiteral("year")).toString();
    job->tags.track = number;
    job->coverUrl = coverUrl;
    job->format = format();
    job->mp3Kbps = m_settings->downloadMp3Kbps();

    const int row = int(m_jobs.size());
    beginInsertRows({}, row, row);
    m_jobs.append(job);
    endInsertRows();
    schedule();
}

void Downloader::schedule()
{
    int active = running();
    const bool skipExisting = m_settings->downloadSkipExisting();
    for (int row = 0; row < m_jobs.size() && active < kParallel; ++row) {
        Job &job = *m_jobs.at(row);
        if (job.state != u"waiting")
            continue;
        // the file of an earlier download is kept as it is
        if (const QString target = targetPath(job); skipExisting && QFileInfo::exists(target)) {
            job.path = target;
            job.state = QStringLiteral("done");
            job.progress = 1;
            qCDebug(lcDownload) << "already there:" << target;
            changed(row);
            continue;
        }
        startJob(row);
        ++active;
    }
    emit runningChanged();
}

void Downloader::startJob(int row)
{
    Job &job = *m_jobs.at(row);
    job.state = QStringLiteral("resolving");
    job.error.clear();
    job.progress = 0;
    job.done = 0;
    job.size = 0;
    job.coverDone = false;
    job.streamDone = false;
    changed(row);
    const QString videoId = job.item.value(QStringLiteral("videoId")).toString();
    // Opus and MP3 start from the best stream, M4A from AAC
    if (job.format == AudioFile::Format::M4a)
        m_resolver->resolveAac(videoId);
    else
        m_resolver->resolve(videoId);
    fetchCover(row);
}

void Downloader::onResolved(const Stream &stream)
{
    if (stream.videoAsked)
        return;
    for (int row = 0; row < m_jobs.size(); ++row) {
        Job &job = *m_jobs.at(row);
        if (job.state != u"resolving" || job.item.value(QStringLiteral("videoId")).toString() != stream.videoId
            || stream.aacAsked != (job.format == AudioFile::Format::M4a))
            continue;
        job.stream = stream;
        job.path = targetPath(job);
        QDir().mkpath(QFileInfo(job.path).absolutePath());
        job.part = std::make_shared<QFile>(job.path + QStringLiteral(".part"));
        if (!job.part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finish(row, job.part->errorString());
            continue;
        }
        // the length is in the URL (clen); else the first answer tells
        job.size = QUrlQuery(stream.url).queryItemValue(QStringLiteral("clen")).toLongLong();
        job.state = QStringLiteral("downloading");
        changed(row);
        fetchChunk(row);
    }
}

void Downloader::onFailed(const QString &videoId, const QString &error)
{
    for (int row = 0; row < m_jobs.size(); ++row) {
        const Job &job = *m_jobs.at(row);
        if (job.state == u"resolving" && job.item.value(QStringLiteral("videoId")).toString() == videoId)
            finish(row, error);
    }
}

void Downloader::fetchChunk(int row)
{
    const std::shared_ptr<Job> job = m_jobs.at(row);
    const qint64 end = job->size > 0 ? qMin(job->done + kChunk, job->size) - 1 : job->done + kChunk - 1;
    QNetworkRequest request(job->stream.url);
    for (const auto &[name, value] : job->stream.headers)
        request.setRawHeader(name, value);
    request.setRawHeader("Range", "bytes=" + QByteArray::number(job->done) + '-' + QByteArray::number(end));
    QNetworkReply *reply = m_network->get(request);
    job->reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, job, reply] {
        const QByteArray data = reply->readAll();
        job->part->write(data);
        job->done += data.size();
        if (job->size > 0)
            job->progress = double(job->done) / double(job->size);
        if (const int r = rowOf(job.get()); r >= 0)
            changed(r);
    });
    connect(reply, &QNetworkReply::finished, this, [this, job, reply] {
        reply->deleteLater();
        const int r = rowOf(job.get());
        if (r < 0)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            finish(r, reply->errorString());
            return;
        }
        if (job->size <= 0) {
            // "bytes 0-10485759/4382911"
            const QByteArray range = reply->rawHeader("Content-Range");
            job->size = range.mid(range.lastIndexOf('/') + 1).toLongLong();
        }
        if (job->size > 0 && job->done < job->size) {
            fetchChunk(r);
            return;
        }
        job->part->close();
        job->streamDone = true;
        maybeWrite(r);
    });
}

void Downloader::fetchCover(int row)
{
    const std::shared_ptr<Job> job = m_jobs.at(row);
    if (job->coverUrl.isEmpty()) {
        job->coverDone = true;
        return;
    }
    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(ytm::artUrl(job->coverUrl, 600))));
    connect(reply, &QNetworkReply::finished, this, [this, job, reply] {
        reply->deleteLater();
        QImage image;
        if (reply->error() == QNetworkReply::NoError && image.loadFromData(reply->readAll())) {
            if (image.width() > 1000 || image.height() > 1000)
                image = image.scaled(1000, 1000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QBuffer buffer(&job->tags.coverJpeg);
            buffer.open(QIODevice::WriteOnly);
            image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPG", 92);
            job->tags.coverWidth = image.width();
            job->tags.coverHeight = image.height();
        }
        // without a cover the file is written all the same
        job->coverDone = true;
        if (const int r = rowOf(job.get()); r >= 0)
            maybeWrite(r);
    });
}

void Downloader::maybeWrite(int row)
{
    const std::shared_ptr<Job> job = m_jobs.at(row);
    if (!job->streamDone || !job->coverDone || job->state != u"downloading")
        return;
    job->state = QStringLiteral("writing");
    job->progress = 1;
    changed(row);

    // the remux (or MP3 encoding) runs off the UI thread
    const QString input = job->part->fileName();
    const QString output = job->path;
    const AudioFile::Format format = job->format;
    const AudioFile::Tags tags = job->tags;
    const int kbps = job->mp3Kbps;
    QPointer<Downloader> self(this);
    QThread *thread = QThread::create([self, job, input, output, format, tags, kbps] {
        const QString error = AudioFile::write(input, output, format, tags, kbps);
        QFile::remove(input);
        if (!error.isEmpty())
            QFile::remove(output);
        QMetaObject::invokeMethod(qApp, [self, job, error] {
            if (!self)
                return;
            if (const int r = self->rowOf(job.get()); r >= 0)
                self->finish(r, error);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

void Downloader::finish(int row, const QString &error)
{
    Job &job = *m_jobs.at(row);
    if (job.reply)
        job.reply->abort();
    if (job.part && !error.isEmpty())
        job.part->remove();
    job.part.reset();
    job.state = error.isEmpty() ? QStringLiteral("done") : QStringLiteral("failed");
    job.error = error;
    if (!error.isEmpty())
        qCWarning(lcDownload) << job.tags.title << error;
    else
        qCDebug(lcDownload) << "written" << job.path;
    changed(row);
    schedule();
}

void Downloader::retry(int row)
{
    if (row < 0 || row >= m_jobs.size() || m_jobs.at(row)->state != u"failed")
        return;
    m_jobs.at(row)->state = QStringLiteral("waiting");
    changed(row);
    schedule();
}

void Downloader::clearFinished()
{
    for (int row = int(m_jobs.size()) - 1; row >= 0; --row) {
        const QString state = m_jobs.at(row)->state;
        if (state == u"done" || state == u"failed") {
            beginRemoveRows({}, row, row);
            m_jobs.removeAt(row);
            endRemoveRows();
        }
    }
}

void Downloader::openFolder(int row)
{
    QString dir = folder();
    if (row >= 0 && row < m_jobs.size() && !m_jobs.at(row)->path.isEmpty())
        dir = QFileInfo(m_jobs.at(row)->path).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void Downloader::changed(int row)
{
    const QModelIndex at = index(row);
    emit dataChanged(at, at);
}

int Downloader::rowOf(const Job *job) const
{
    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).get() == job)
            return row;
    }
    return -1;
}

// <folder>/<artist>/<album>/<NN title>.<ext>, or <folder>/<artist>/<title>.<ext>
QString Downloader::targetPath(const Job &job) const
{
    const QString artist = safeName(!job.tags.albumArtist.isEmpty() ? job.tags.albumArtist
                                    : !job.tags.artist.isEmpty() ? job.tags.artist.section(u',', 0, 0)
                                                                 : tr("Unknown artist"));
    QString dir = folder() + u'/' + artist;
    if (!job.tags.album.isEmpty())
        dir += u'/' + safeName(job.tags.album);
    QString name = safeName(job.tags.title);
    if (job.tags.track > 0)
        name = QStringLiteral("%1 %2").arg(job.tags.track, 2, 10, QLatin1Char('0')).arg(name);
    const QString ext = AudioFile::extension(job.format);
    QString path = QStringLiteral("%1/%2.%3").arg(dir, name, ext);
    for (int n = 2; QFile::exists(path); ++n)
        path = QStringLiteral("%1/%2 (%3).%4").arg(dir, name).arg(n).arg(ext);
    return path;
}
