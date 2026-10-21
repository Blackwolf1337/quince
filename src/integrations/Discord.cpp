#include "integrations/Discord.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "ytm/Parser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>
#include <QtEndian>

#include <array>
#include <cmath>
#include <cstring>

Q_LOGGING_CATEGORY(lcDiscord, "quince.discord")

namespace {

constexpr int kRetryMs = 20000;
// Discord takes five activity updates per 20 seconds
constexpr int kUpdateDelayMs = 1000;
// a position jump larger than this is a seek and moves the progress bar
constexpr double kSeekThreshold = 2.0;
constexpr int kMaxText = 128;
constexpr int kCoverPixels = 512;

QString clip(const QString &text)
{
    // Discord wants at least two characters and at most 128
    QString out = text.left(kMaxText);
    while (out.size() < 2)
        out += u' ';
    return out;
}

} // namespace

Discord::Discord(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &Discord::connectNext);

    m_update.setSingleShot(true);
    m_update.setInterval(kUpdateDelayMs);
    connect(&m_update, &QTimer::timeout, this, &Discord::sendActivity);

    m_pauseTimeout.setSingleShot(true);
    connect(&m_pauseTimeout, &QTimer::timeout, this, [this] {
        m_pausedTooLong = true;
        scheduleUpdate();
    });

    connect(m_settings, &Settings::changed, this, [this] {
        applySettings();
        onPausedChanged();
        scheduleUpdate();
    });
    connect(m_playback, &Playback::trackChanged, this, &Discord::scheduleUpdate);
    connect(m_player, &MpvPlayer::pausedChanged, this, &Discord::onPausedChanged);
    connect(m_player, &MpvPlayer::idleChanged, this, &Discord::scheduleUpdate);
    connect(m_player, &MpvPlayer::positionChanged, this, [this] {
        const double position = m_player->position();
        if (std::abs(position - m_lastPosition) > kSeekThreshold)
            scheduleUpdate();
        m_lastPosition = position;
    });
    applySettings();
}

QStringList Discord::socketCandidates()
{
    QStringList names;
#ifdef Q_OS_WIN
    // \\.\pipe\discord-ipc-N
    for (int i = 0; i < 10; ++i)
        names.append(QStringLiteral("discord-ipc-%1").arg(i));
#else
    QStringList dirs;
    for (const char *var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const QString dir = qEnvironmentVariable(var);
        if (!dir.isEmpty())
            dirs.append(dir);
    }
    dirs.append(QStringLiteral("/tmp"));
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) {
        // Flatpak and Snap builds of Discord
        dirs.append(runtime + QStringLiteral("/app/com.discordapp.Discord"));
        dirs.append(runtime + QStringLiteral("/snap.discord"));
    }
    dirs.removeDuplicates();
    for (const QString &dir : std::as_const(dirs)) {
        for (int i = 0; i < 10; ++i) {
            const QString path = dir + QStringLiteral("/discord-ipc-%1").arg(i);
            if (QFileInfo::exists(path))
                names.append(path);
        }
    }
#endif
    return names;
}

void Discord::applySettings()
{
    const QString appId = m_settings->discord() ? m_settings->discordAppId() : QString();
    if (appId == m_appId)
        return;
    disconnectNow();
    m_appId = appId;
    if (!m_appId.isEmpty()) {
        m_candidates = socketCandidates();
        connectNext();
    }
}

void Discord::disconnectNow()
{
    m_retry.stop();
    m_ready = false;
    m_buffer.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void Discord::connectNext()
{
    if (m_appId.isEmpty())
        return;
    if (m_candidates.isEmpty()) {
        // no client running now; look again later
        m_candidates = socketCandidates();
        m_retry.start();
        return;
    }

    disconnectNow();
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, &Discord::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &Discord::onReadyRead);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this] {
        qCDebug(lcDiscord) << "socket:" << m_socket->errorString();
        QTimer::singleShot(0, this, &Discord::connectNext);
    });
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        m_ready = false;
        m_candidates = socketCandidates();
        m_retry.start();
    });
    m_socket->connectToServer(m_candidates.takeFirst());
}

void Discord::onConnected()
{
    send(Handshake, QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_appId}})
                        .toJson(QJsonDocument::Compact));
}

void Discord::onReadyRead()
{
    m_buffer += m_socket->readAll();
    // frames: opcode and length as little endian uint32, then JSON
    while (m_buffer.size() >= 8) {
        std::array<uchar, 8> header{};
        std::memcpy(header.data(), m_buffer.constData(), header.size());
        const quint32 opcode = qFromLittleEndian<quint32>(header.data());
        const quint32 length = qFromLittleEndian<quint32>(header.data() + 4);
        if (static_cast<quint32>(m_buffer.size()) < 8 + length)
            return;
        const QJsonObject message = QJsonDocument::fromJson(m_buffer.mid(8, length)).object();
        m_buffer.remove(0, 8 + length);

        if (opcode == Close) {
            qCWarning(lcDiscord) << "Discord closed the connection:" << message.value(u"message").toString();
            disconnectNow();
            m_retry.start();
            return;
        }
        if (message.value(u"evt").toString() == u"READY") {
            m_ready = true;
            sendActivity();
        } else if (message.value(u"evt").toString() == u"ERROR") {
            qCWarning(lcDiscord) << "Discord:" << message.value(u"data").toObject().value(u"message").toString();
        }
    }
}

void Discord::send(quint32 opcode, const QByteArray &json)
{
    if (!m_socket)
        return;
    std::array<uchar, 8> header{};
    qToLittleEndian<quint32>(opcode, header.data());
    qToLittleEndian<quint32>(static_cast<quint32>(json.size()), header.data() + 4);
    m_socket->write(QByteArray(reinterpret_cast<const char *>(header.data()), header.size()) + json);
}

void Discord::onPausedChanged()
{
    const int minutes = m_settings->discordPauseMinutes();
    if (m_player->paused() && minutes > 0) {
        // a settings change while paused keeps the time already waited
        if (!m_pauseTimeout.isActive() && !m_pausedTooLong)
            m_pauseTimeout.start(minutes * 60 * 1000);
    } else {
        m_pauseTimeout.stop();
        m_pausedTooLong = false;
    }
    scheduleUpdate();
}

void Discord::scheduleUpdate()
{
    if (m_ready && !m_update.isActive())
        m_update.start();
}

void Discord::sendActivity()
{
    if (!m_ready)
        return;

    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    QJsonObject args{{QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())}};

    if (!videoId.isEmpty() && !m_player->idle() && !m_pausedTooLong) {
        QString artists = track.value(QStringLiteral("artistText")).toString();
        if (artists.isEmpty()) {
            QStringList names;
            for (const QVariant &a : track.value(QStringLiteral("artists")).toList())
                names.append(a.toMap().value(QStringLiteral("name")).toString());
            artists = names.join(QStringLiteral(", "));
        }
        const QString album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
        const QString title = track.value(QStringLiteral("title")).toString();

        QJsonObject assets{{QStringLiteral("large_text"), clip(album.isEmpty() ? title : album)}};
        const QString cover = ytm::artUrl(track.value(QStringLiteral("thumbnail")).toString(), kCoverPixels);
        if (cover.startsWith(u"https://"))
            assets.insert(QStringLiteral("large_image"), cover);

        // what the member list shows after the name: 0 the app, 1 state, 2 details
        const QString shows = m_settings->discordShows();
        QJsonObject activity{
            {QStringLiteral("type"), 2}, // "Listening to"
            {QStringLiteral("details"), clip(title)},
            {QStringLiteral("state"), clip(artists)},
            {QStringLiteral("assets"), assets},
            {QStringLiteral("status_display_type"), shows == u"artist" ? 1 : shows == u"title" ? 2 : 0},
        };
        if (m_settings->discordButton()) {
            activity.insert(QStringLiteral("buttons"),
                            QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("Listen on YouTube Music")},
                                                   {QStringLiteral("url"),
                                                    QStringLiteral("https://music.youtube.com/watch?v=") + videoId}}});
        }
        if (!m_player->paused() && m_player->duration() > 0) {
            const qint64 start = QDateTime::currentMSecsSinceEpoch()
                                 - static_cast<qint64>(m_player->position() * 1000.0);
            QJsonObject timestamps{{QStringLiteral("start"), start}};
            // without the end Discord counts up instead of showing the time left
            if (!m_settings->discordHideTimeLeft())
                timestamps.insert(QStringLiteral("end"), start + static_cast<qint64>(m_player->duration() * 1000.0));
            activity.insert(QStringLiteral("timestamps"), timestamps);
        }
        args.insert(QStringLiteral("activity"), activity);
    }

    const QJsonObject command{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), args},
        {QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
    };
    send(Frame, QJsonDocument(command).toJson(QJsonDocument::Compact));
}
