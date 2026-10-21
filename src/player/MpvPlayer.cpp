#include "player/MpvPlayer.h"
#include "player/MpvNode.h"
#include "player/StreamResolver.h"

#include <QLoggingCategory>

#include <mpv/client.h>

#include <cmath>

Q_LOGGING_CATEGORY(lcPlayer, "quince.player")

namespace {

enum Property : quint64 {
    PropTimePos = 1,
    PropDuration,
    PropPause,
    PropIdle,
    PropBuffering,
    PropVolume,
    PropMute,
    PropAudioDevices,
    PropAudioDevice,
    PropCurrentAo,
};

// position updates closer than this are not sent to the UI
constexpr double kPositionStep = 0.1;

// a string list option value is split at ',' and unescaped with '\'
QString escapeListItem(QString item)
{
    item.replace(u'\\', QStringLiteral("\\\\"));
    item.replace(u',', QStringLiteral("\\,"));
    return item;
}

double doubleOf(const mpv_event_property *prop)
{
    return prop->format == MPV_FORMAT_DOUBLE ? *static_cast<double *>(prop->data) : 0.0;
}

bool flagOf(const mpv_event_property *prop)
{
    return prop->format == MPV_FORMAT_FLAG && *static_cast<int *>(prop->data) != 0;
}

} // namespace

MpvPlayer::MpvPlayer(const QString &audioOutput, QObject *parent)
    : QObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        qCCritical(lcPlayer) << "mpv_create failed";
        return;
    }

    mpv_set_option_string(m_mpv, "video", "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "idle", "yes");
    mpv_set_option_string(m_mpv, "gapless-audio", "yes");
    mpv_set_option_string(m_mpv, "prefetch-playlist", "yes");
    mpv_set_option_string(m_mpv, "audio-client-name", "Quince");
    if (!audioOutput.isEmpty())
        mpv_set_option_string(m_mpv, "ao", audioOutput.toUtf8().constData());

    if (mpv_initialize(m_mpv) < 0) {
        qCCritical(lcPlayer) << "mpv_initialize failed";
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_request_log_messages(m_mpv, "warn");
    mpv_observe_property(m_mpv, PropTimePos, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropDuration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropPause, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropIdle, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropBuffering, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropVolume, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, PropMute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, PropAudioDevices, "audio-device-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, PropAudioDevice, "audio-device", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, PropCurrentAo, "current-ao", MPV_FORMAT_STRING);

    mpv_set_wakeup_callback(m_mpv, &MpvPlayer::wakeup, this);
}

MpvPlayer::~MpvPlayer()
{
    if (!m_mpv)
        return;
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    mpv_terminate_destroy(m_mpv);
}

// Called on an mpv thread.
void MpvPlayer::wakeup(void *ctx)
{
    auto *self = static_cast<MpvPlayer *>(ctx);
    if (self->m_wakePending.exchange(true))
        return;
    QMetaObject::invokeMethod(self, [self] { self->processEvents(); }, Qt::QueuedConnection);
}

void MpvPlayer::processEvents()
{
    m_wakePending = false;
    while (m_mpv) {
        const mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(*event);
    }
}

void MpvPlayer::handleEvent(const mpv_event &event)
{
    switch (event.event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *prop = static_cast<const mpv_event_property *>(event.data);
        switch (event.reply_userdata) {
        case PropTimePos:
            m_position = doubleOf(prop);
            if (std::abs(m_position - m_reportedPosition) >= kPositionStep || m_position == 0) {
                m_reportedPosition = m_position;
                emit positionChanged();
            }
            break;
        case PropDuration:
            m_duration = doubleOf(prop);
            emit durationChanged();
            break;
        case PropPause:
            m_paused = flagOf(prop);
            emit pausedChanged();
            break;
        case PropIdle:
            m_idle = flagOf(prop);
            emit idleChanged();
            break;
        case PropBuffering:
            m_buffering = flagOf(prop);
            emit bufferingChanged();
            break;
        case PropVolume:
            m_volume = doubleOf(prop);
            emit volumeChanged();
            break;
        case PropMute:
            m_muted = flagOf(prop);
            emit mutedChanged();
            break;
        case PropAudioDevices:
            m_allAudioDevices = prop->format == MPV_FORMAT_NODE
                                    ? MpvNode::toVariant(static_cast<const mpv_node *>(prop->data)).toList()
                                    : QVariantList();
            filterAudioDevices();
            break;
        case PropCurrentAo:
            // empty until the first file opened the audio output
            if (prop->format == MPV_FORMAT_STRING) {
                m_currentAo = QString::fromUtf8(*static_cast<const char *const *>(prop->data));
                filterAudioDevices();
            }
            break;
        case PropAudioDevice:
            m_audioDevice = prop->format == MPV_FORMAT_STRING
                                ? QString::fromUtf8(*static_cast<const char *const *>(prop->data))
                                : QString();
            emit audioDeviceChanged();
            break;
        }
        break;
    }
    case MPV_EVENT_START_FILE: {
        const auto *start = static_cast<const mpv_event_start_file *>(event.data);
        const auto it = m_entryTags.constFind(start->playlist_entry_id);
        if (it != m_entryTags.cend())
            emit trackStarted(*it);
        else
            m_unmatchedStart = start->playlist_entry_id;
        break;
    }
    case MPV_EVENT_END_FILE: {
        const auto *end = static_cast<const mpv_event_end_file *>(event.data);
        const QString tag = m_entryTags.take(end->playlist_entry_id);
        QString error;
        if (end->reason == MPV_END_FILE_REASON_ERROR) {
            error = QString::fromUtf8(mpv_error_string(end->error));
            emit playbackError(error);
        }
        emit trackEnded(tag, end->reason == MPV_END_FILE_REASON_EOF, error);
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        const auto *msg = static_cast<const mpv_event_log_message *>(event.data);
        qCWarning(lcPlayer).noquote() << msg->prefix << QString::fromUtf8(msg->text).trimmed();
        break;
    }
    case MPV_EVENT_COMMAND_REPLY: {
        const QString tag = m_replyTags.take(event.reply_userdata);
        if (event.error < 0) {
            qCWarning(lcPlayer) << "mpv command failed:" << mpv_error_string(event.error);
            break;
        }
        if (tag.isEmpty())
            break;
        const auto *cmd = static_cast<const mpv_event_command *>(event.data);
        const qint64 entry = MpvNode::toVariant(&cmd->result).toMap()
                                 .value(QStringLiteral("playlist_entry_id"), -1).toLongLong();
        if (entry < 0)
            break;
        m_entryTags.insert(entry, tag);
        // the file may have started before this reply arrived
        if (entry == m_unmatchedStart) {
            m_unmatchedStart = -1;
            emit trackStarted(tag);
        }
        break;
    }
    case MPV_EVENT_PLAYBACK_RESTART:
        emit playbackRestarted();
        break;
    case MPV_EVENT_SET_PROPERTY_REPLY:
        if (event.error < 0)
            qCWarning(lcPlayer) << "mpv property failed:" << mpv_error_string(event.error);
        break;
    default:
        break;
    }
}

void MpvPlayer::command(const QVariant &args, quint64 reply)
{
    if (!m_mpv)
        return;
    MpvNode node(args);
    mpv_command_node_async(m_mpv, reply, node.get());
}

void MpvPlayer::setMpvProperty(const char *name, const QVariant &value)
{
    if (!m_mpv)
        return;
    MpvNode node(value);
    mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_NODE, node.get());
}

void MpvPlayer::loadFile(const Stream &stream, const QString &tag, const char *flags, double start, double gainDb)
{
    QVariantMap options;
    QStringList fields;
    for (const auto &[name, value] : stream.headers) {
        if (name.compare("User-Agent", Qt::CaseInsensitive) == 0)
            options.insert(QStringLiteral("user-agent"), QString::fromUtf8(value));
        else
            fields.append(escapeListItem(QString::fromUtf8(name + ": " + value)));
    }
    if (!fields.isEmpty())
        options.insert(QStringLiteral("http-header-fields"), fields.join(u','));
    // googlevideo refuses open-ended requests for some URLs (403) and throttles
    // long ones; bounded 10 MiB ranges are what yt-dlp asks for too
    options.insert(QStringLiteral("stream-lavf-o"), QStringLiteral("request_size=10485760,multiple_requests=1"));
    if (start > 0)
        options.insert(QStringLiteral("start"), QString::number(start, 'f', 3));
    // a per-file option: mpv puts the old value back when the file ends
    options.insert(QStringLiteral("volume-gain"), QString::number(gainDb, 'f', 2));

    const quint64 reply = m_nextReply++;
    m_replyTags.insert(reply, tag);
    command(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("loadfile")},
        {QStringLiteral("url"), stream.url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("flags"), QString::fromLatin1(flags)},
        {QStringLiteral("index"), -1},
        {QStringLiteral("options"), options},
    }, reply);
}

void MpvPlayer::load(const Stream &stream, const QString &tag, double start, bool paused, double gainDb)
{
    m_position = m_reportedPosition = start;
    emit positionChanged();
    setPaused(paused);
    loadFile(stream, tag, "replace", start, gainDb);
}

void MpvPlayer::queueNext(const Stream &stream, const QString &tag, double gainDb)
{
    clearQueued();
    loadFile(stream, tag, "append", 0, gainDb);
}

// mpv lists every backend (ALSA plugins, JACK, OSS ...); the outputs of the
// backend in use are the ones a person means
void MpvPlayer::filterAudioDevices()
{
    QString backend = m_currentAo;
    if (backend.isEmpty() && !m_allAudioDevices.isEmpty()) {
        // before playback: the first backend mpv lists is the one it will try
        for (const QVariant &v : std::as_const(m_allAudioDevices)) {
            const QString name = v.toMap().value(QStringLiteral("name")).toString();
            if (name.contains(u'/')) {
                backend = name.section(u'/', 0, 0);
                break;
            }
        }
    }
    QVariantList devices;
    for (const QVariant &v : std::as_const(m_allAudioDevices)) {
        const QString name = v.toMap().value(QStringLiteral("name")).toString();
        if (name == u"auto") {
            // mpv's own label is English only
            QVariantMap automatic = v.toMap();
            automatic.insert(QStringLiteral("description"), tr("System default"));
            devices.append(automatic);
        } else if (backend.isEmpty() || name.startsWith(backend + u'/')) {
            devices.append(v);
        }
    }
    if (devices != m_audioDevices) {
        m_audioDevices = devices;
        emit audioDevicesChanged();
    }
}

void MpvPlayer::setGain(double gainDb)
{
    setMpvProperty("volume-gain", gainDb);
}

void MpvPlayer::setAudioDevice(const QString &device)
{
    setMpvProperty("audio-device", device.isEmpty() ? QStringLiteral("auto") : device);
}

void MpvPlayer::clearQueued()
{
    // removes every entry except the one playing
    command(QVariantList{QStringLiteral("playlist-clear")});
}

void MpvPlayer::setPaused(bool paused)
{
    setMpvProperty("pause", paused);
}

void MpvPlayer::setVolume(double volume)
{
    setMpvProperty("volume", qBound(0.0, volume, 100.0));
}

void MpvPlayer::setMuted(bool muted)
{
    setMpvProperty("mute", muted);
}

void MpvPlayer::togglePause()
{
    command(QVariantList{QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvPlayer::seek(double seconds)
{
    command(QVariantList{QStringLiteral("seek"), seconds, QStringLiteral("absolute")});
}

void MpvPlayer::stop()
{
    command(QVariantList{QStringLiteral("stop")});
}
