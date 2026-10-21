#include "player/StreamResolver.h"
#include "ytm/AccountCookies.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcResolve, "quince.resolve")

namespace {

constexpr int kTimeoutMs = 45000;
// a URL closer than this to its expire time is resolved again
constexpr qint64 kExpireMarginSecs = 10 * 60;

// 774 is the 256 kbit/s Opus format YouTube Music serves to Premium accounts,
// 251 the 160 kbit/s Opus format everybody gets.
const QString kFormat = QStringLiteral("774/251/bestaudio");

QString errorLine(const QByteArray &stderrText)
{
    const QStringList lines = QString::fromUtf8(stderrText).split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (it->startsWith(u"ERROR: "))
            return it->mid(7).trimmed();
    }
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

QString artistOf(const QJsonObject &info)
{
    QStringList names;
    for (const QJsonValue &v : info.value(u"artists").toArray())
        names.append(v.toString());
    if (!names.isEmpty())
        return names.join(u", ");
    QString name = info.value(u"artist").toString();
    if (name.isEmpty())
        name = info.value(u"uploader").toString();
    if (name.endsWith(u" - Topic"))
        name.chop(8);
    return name;
}

} // namespace

StreamResolver::StreamResolver(QObject *parent)
    : QObject(parent)
{
}

StreamResolver::~StreamResolver()
{
    for (QProcess *process : std::as_const(m_running)) {
        process->disconnect(this);
        process->kill();
    }
}

void StreamResolver::setCookies(AccountCookies *cookies)
{
    m_cookies = cookies;
}

bool StreamResolver::isCached(const QString &videoId) const
{
    const auto it = m_cache.constFind(videoId);
    if (it == m_cache.cend())
        return false;
    return QDateTime::currentDateTimeUtc().secsTo(it->expires) > kExpireMarginSecs;
}

void StreamResolver::forget(const QString &videoId)
{
    m_cache.remove(videoId);
}

bool StreamResolver::isRunning(const QString &videoId) const
{
    return m_running.contains(videoId);
}

QString StreamResolver::program()
{
    const QString local = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"),
                                                         {QCoreApplication::applicationDirPath()});
    if (!local.isEmpty())
        return local;
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

void StreamResolver::fail(const QString &videoId, const QString &error)
{
    qCWarning(lcResolve) << videoId << error;
    emit failed(videoId, error);
}

void StreamResolver::resolve(const QString &videoId)
{
    if (isCached(videoId)) {
        QMetaObject::invokeMethod(this, [this, videoId] {
            emit resolved(m_cache.value(videoId));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_running.contains(videoId))
        return;

    const QString exe = program();
    if (exe.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, videoId] {
            fail(videoId, tr("yt-dlp was not found. Install it, or put it next to Quince."));
        }, Qt::QueuedConnection);
        return;
    }

    QStringList args{
        QStringLiteral("--ignore-config"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--format"), kFormat,
    };
    auto *process = new QProcess(this);
    // the copy lives as long as the process
    auto *cookieCopy = new QTemporaryFile(process);
    if (m_cookies && m_cookies->writeCopy(*cookieCopy))
        args << QStringLiteral("--cookies") << cookieCopy->fileName();
    args << QStringLiteral("https://music.youtube.com/watch?v=") + videoId;

    m_running.insert(videoId, process);

    connect(process, &QProcess::finished, this, [this, process, videoId] {
        onFinished(process, videoId);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, videoId](QProcess::ProcessError error) {
        // a process that never started emits no finished signal
        if (error != QProcess::FailedToStart)
            return;
        m_running.remove(videoId);
        process->deleteLater();
        fail(videoId, tr("yt-dlp could not be started: %1").arg(process->errorString()));
    });
    QTimer::singleShot(kTimeoutMs, process, [process] {
        process->kill();
    });

    qCDebug(lcResolve) << "resolving" << videoId;
    process->start(exe, args);
}

void StreamResolver::onFinished(QProcess *process, const QString &videoId)
{
    m_running.remove(videoId);
    process->deleteLater();

    if (process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0) {
        QString error = errorLine(process->readAllStandardError());
        if (error.isEmpty())
            error = tr("yt-dlp stopped without a result (timeout or crash).");
        fail(videoId, error);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(process->readAllStandardOutput(), &parseError);
    const QJsonObject info = doc.object();
    if (parseError.error != QJsonParseError::NoError || info.isEmpty()) {
        fail(videoId, tr("yt-dlp gave no readable answer: %1").arg(parseError.errorString()));
        return;
    }

    Stream stream;
    stream.videoId = videoId;
    stream.url = QUrl(info.value(u"url").toString());
    if (!stream.url.isValid() || stream.url.isEmpty()) {
        fail(videoId, tr("yt-dlp found no audio stream."));
        return;
    }

    const QJsonObject headers = info.value(u"http_headers").toObject();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        stream.headers.append({it.key().toUtf8(), it.value().toString().toUtf8()});

    stream.formatId = info.value(u"format_id").toString();
    stream.codec = info.value(u"acodec").toString();
    stream.bitrate = info.value(u"abr").toDouble();
    stream.duration = info.value(u"duration").toDouble();

    const qint64 expire = QUrlQuery(stream.url).queryItemValue(QStringLiteral("expire")).toLongLong();
    stream.expires = expire > 0 ? QDateTime::fromSecsSinceEpoch(expire, QTimeZone::UTC)
                                : QDateTime::currentDateTimeUtc().addSecs(3600);

    stream.title = info.value(u"track").toString();
    if (stream.title.isEmpty())
        stream.title = info.value(u"title").toString();
    stream.artist = artistOf(info);
    stream.album = info.value(u"album").toString();
    stream.thumbnail = QUrl(info.value(u"thumbnail").toString());

    qCDebug(lcResolve) << "resolved" << videoId << stream.formatId << stream.codec << stream.bitrate << "kbit/s";
    m_cache.insert(videoId, stream);
    emit resolved(stream);
}

QString StreamResolver::extractVideoId(const QString &text)
{
    static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    static const QRegularExpression inUrl(
        QStringLiteral("(?:[?&]v=|youtu\\.be/|/shorts/|/embed/|/live/)([A-Za-z0-9_-]{11})"));

    const QString trimmed = text.trimmed();
    if (bare.match(trimmed).hasMatch())
        return trimmed;
    const QRegularExpressionMatch match = inUrl.match(trimmed);
    return match.hasMatch() ? match.captured(1) : QString();
}
