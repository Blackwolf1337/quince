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
    PropMute,
    PropAudioDevices,
    PropAudioDevice,
    PropCurrentAo,
    PropSilence,
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
    , m_audioOutput(audioOutput)
{
    m_fadeTimer.setInterval(30);
    connect(&m_fadeTimer, &QTimer::timeout, this, &MpvPlayer::stepFade);
    // the second deck is made at the first crossfade
    createDeck(0);
}

MpvPlayer::~MpvPlayer()
{
    for (Deck &deck : m_decks) {
        if (!deck.mpv)
            continue;
        mpv_set_wakeup_callback(deck.mpv, nullptr, nullptr);
        mpv_terminate_destroy(deck.mpv);
        deck.mpv = nullptr;
    }
}

bool MpvPlayer::createDeck(int index)
{
    mpv_handle *mpv = mpv_create();
    if (!mpv) {
        qCCritical(lcPlayer) << "mpv_create failed";
        return false;
    }

    // video only in video mode (vid per file), drawn by MpvVideoItem
    mpv_set_option_string(mpv, "vid", "no");
    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "hwdec", "auto-safe");
    // pictures with alpha (the visualizer) stay see-through in the video item
    mpv_set_option_string(mpv, "alpha", "yes");
    mpv_set_option_string(mpv, "background", "none");
    mpv_set_option_string(mpv, "audio-display", "no");
    mpv_set_option_string(mpv, "keep-open", "no");
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "terminal", "no");
    mpv_set_option_string(mpv, "input-default-bindings", "no");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "gapless-audio", "yes");
    mpv_set_option_string(mpv, "prefetch-playlist", "yes");
    mpv_set_option_string(mpv, "audio-client-name", "Quince");
    if (!m_audioOutput.isEmpty())
        mpv_set_option_string(mpv, "ao", m_audioOutput.toUtf8().constData());

    if (mpv_initialize(mpv) < 0) {
        qCCritical(lcPlayer) << "mpv_initialize failed";
        mpv_destroy(mpv);
        return false;
    }

    // QUINCE_MPV_LOG=v (or debug, info) shows more of mpv's log (diagnosis)
    const QByteArray logLevel = qEnvironmentVariableIsSet("QUINCE_MPV_LOG") ? qgetenv("QUINCE_MPV_LOG") : QByteArray("warn");
    mpv_request_log_messages(mpv, logLevel.constData());
    mpv_observe_property(mpv, PropTimePos, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, PropDuration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, PropPause, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, PropIdle, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, PropBuffering, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, PropMute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, PropAudioDevice, "audio-device", MPV_FORMAT_STRING);
    mpv_observe_property(mpv, PropCurrentAo, "current-ao", MPV_FORMAT_STRING);
    mpv_observe_property(mpv, PropSilence, "af-metadata/silence", MPV_FORMAT_NODE);

    m_decks[index].mpv = mpv;
    // what the other deck already has
    if (index != 0) {
        setDeckProperty(index, "volume", 0.0);
        setDeckProperty(index, "pause", m_paused);
        setDeckProperty(index, "mute", m_muted);
        if (!m_audioDeviceWanted.isEmpty())
            setDeckProperty(index, "audio-device", m_audioDeviceWanted);
    }
    if (!m_filters.isEmpty())
        setDeckProperty(index, "af", m_filters);
    if (m_speed * m_speedBoost != 1.0)
        setDeckProperty(index, "speed", m_speed * m_speedBoost);

    mpv_set_wakeup_callback(mpv, &MpvPlayer::wakeup, this);
    return true;
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
    for (int deck = 0; deck < 2; ++deck) {
        while (m_decks[deck].mpv) {
            const mpv_event *event = mpv_wait_event(m_decks[deck].mpv, 0);
            if (event->event_id == MPV_EVENT_NONE)
                break;
            handleEvent(deck, *event);
        }
    }
}

void MpvPlayer::handleEvent(int deckIndex, const mpv_event &event)
{
    Deck &deck = m_decks[deckIndex];
    const bool active = deckIndex == m_active;

    switch (event.event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        // the idle deck and the one fading out do not speak for the player
        if (!active)
            break;
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
        case PropSilence:
            if (prop->format == MPV_FORMAT_NODE)
                emit silenceMetadata(MpvNode::toVariant(static_cast<const mpv_node *>(prop->data)).toMap());
            break;
        }
        break;
    }
    case MPV_EVENT_START_FILE: {
        const auto *start = static_cast<const mpv_event_start_file *>(event.data);
        const auto it = deck.entryTags.constFind(start->playlist_entry_id);
        if (it != deck.entryTags.cend())
            onStarted(deckIndex, *it);
        else
            deck.unmatchedStart = start->playlist_entry_id;
        break;
    }
    case MPV_EVENT_END_FILE: {
        const auto *end = static_cast<const mpv_event_end_file *>(event.data);
        const QString tag = deck.entryTags.take(end->playlist_entry_id);
        if (!active) {
            // the crossfade's track did not even start
            if (!tag.isEmpty() && tag == m_crossfadeTag) {
                qCWarning(lcPlayer) << "crossfade to" << tag << "failed:" << mpv_error_string(end->error);
                m_crossfadeTag.clear();
                emit crossfadeFailed(tag);
            }
            // the deck that faded out ends quietly
            break;
        }
        if (!tag.isEmpty() && tag == deck.startingTag) {
            deck.startingTag.clear();
            deck.waitingNext.reset();
        }
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
        qCWarning(lcPlayer).noquote() << (deckIndex ? "[deck 2]" : "") << msg->prefix
                                      << QString::fromUtf8(msg->text).trimmed();
        break;
    }
    case MPV_EVENT_COMMAND_REPLY: {
        const QString tag = deck.replyTags.take(event.reply_userdata);
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
        deck.entryTags.insert(entry, tag);
        // the file may have started before this reply arrived
        if (entry == deck.unmatchedStart) {
            deck.unmatchedStart = -1;
            onStarted(deckIndex, tag);
        }
        break;
    }
    case MPV_EVENT_PLAYBACK_RESTART:
        if (!active)
            break;
        // the faded-in track makes sound now: the curve starts
        if (m_fadeFrom >= 0 && !m_fadeTimer.isActive())
            beginFade();
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

void MpvPlayer::command(int deck, const QVariant &args, quint64 reply)
{
    if (!m_decks[deck].mpv)
        return;
    MpvNode node(args);
    mpv_command_node_async(m_decks[deck].mpv, reply, node.get());
}

void MpvPlayer::setDeckProperty(int deck, const char *name, const QVariant &value)
{
    if (!m_decks[deck].mpv)
        return;
    MpvNode node(value);
    mpv_set_property_async(m_decks[deck].mpv, 0, name, MPV_FORMAT_NODE, node.get());
}

void MpvPlayer::setMpvProperty(const char *name, const QVariant &value)
{
    for (int deck = 0; deck < 2; ++deck)
        setDeckProperty(deck, name, value);
}

void MpvPlayer::loadFile(int deck, const Stream &stream, const QString &tag, const char *flags, double start,
                         double gainDb)
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

    QString url = stream.url.toString(QUrl::FullyEncoded);
    if (stream.video) {
        // picture and sound are two streams; an EDL joins them into one file,
        // the way mpv's own ytdl hook does
        const auto part = [](const QString &u) { return u'%' + QString::number(u.toUtf8().size()) + u'%' + u; };
        url = QStringLiteral("edl://!no_clip;!no_chapters;") + part(url)
            + QStringLiteral(";!new_stream;!no_clip;!no_chapters;") + part(stream.audioUrl.toString(QUrl::FullyEncoded));
        options.insert(QStringLiteral("vid"), m_videoOutput ? QStringLiteral("auto") : QStringLiteral("no"));
    } else if (!m_visualizer.isEmpty() && m_videoOutput) {
        options.insert(QStringLiteral("lavfi-complex"), m_visualizer);
    }

    if (m_tagIsVideo.size() > 64)
        m_tagIsVideo.clear();
    m_tagIsVideo.insert(tag, stream.video);
    m_tagPicture.insert(tag, stream.video && m_videoOutput);
    if (m_tagPicture.size() > 64)
        m_tagPicture.clear();
    const quint64 reply = m_nextReply++;
    m_decks[deck].replyTags.insert(reply, tag);
    command(deck, QVariantMap{
        {QStringLiteral("name"), QStringLiteral("loadfile")},
        {QStringLiteral("url"), url},
        {QStringLiteral("flags"), QString::fromLatin1(flags)},
        {QStringLiteral("index"), -1},
        {QStringLiteral("options"), options},
    }, reply);
}

void MpvPlayer::load(const Stream &stream, const QString &tag, double start, bool paused, double gainDb)
{
    abortCrossfade();
    m_position = m_reportedPosition = start;
    emit positionChanged();
    setPaused(paused);
    Deck &deck = m_decks[m_active];
    deck.waitingNext.reset();
    deck.startingTag = tag;
    m_activeIsVideo = stream.video;
    m_activePicture = stream.video && m_videoOutput;
    loadFile(m_active, stream, tag, "replace", start, gainDb);
}

void MpvPlayer::queueNext(const Stream &stream, const QString &tag, double gainDb)
{
    Deck &deck = m_decks[m_active];
    if (!deck.startingTag.isEmpty()) {
        deck.waitingNext = Next{stream, tag, gainDb};
        return;
    }
    clearQueued();
    loadFile(m_active, stream, tag, "append", 0, gainDb);
}

void MpvPlayer::onStarted(int deckIndex, const QString &tag)
{
    Deck &deck = m_decks[deckIndex];
    if (deckIndex != m_active ? tag == m_crossfadeTag : true) {
        m_activeIsVideo = m_tagIsVideo.value(tag);
        m_activePicture = m_tagPicture.value(tag);
    }
    if (deckIndex != m_active) {
        if (tag != m_crossfadeTag)
            return;
        // the next track runs on the other deck: it is the player now, the
        // old deck fades out once the new one makes sound
        m_crossfadeTag.clear();
        m_fadeFrom = m_active;
        m_active = deckIndex;
        emit activeDeckChanged();
        m_position = m_reportedPosition = 0;
        m_duration = 0;
        m_idle = false;
        emit positionChanged();
        emit durationChanged();
        emit idleChanged();
        emit trackStarted(tag);
        return;
    }
    if (tag == deck.startingTag) {
        deck.startingTag.clear();
        if (deck.waitingNext) {
            const Next next = *std::exchange(deck.waitingNext, std::nullopt);
            queueNext(next.stream, next.tag, next.gainDb);
        }
    }
    emit trackStarted(tag);
}

void MpvPlayer::crossfadeTo(const Stream &stream, const QString &tag, double gainDb, double seconds,
                            double inSeconds)
{
    abortCrossfade();
    const int other = 1 - m_active;
    if (!m_decks[other].mpv && !createDeck(other)) {
        emit crossfadeFailed(tag);
        return;
    }
    // nothing of the old deck may follow on it by itself
    clearQueued();
    m_crossfadeTag = tag;
    m_fadeSeconds = qMax(0.5, seconds);
    m_fadeInSeconds = qBound(0.5, inSeconds, m_fadeSeconds);
    m_decks[other].startingTag.clear();
    m_decks[other].waitingNext.reset();
    setDeckProperty(other, "volume", 0.0);
    setDeckProperty(other, "pause", m_paused);
    loadFile(other, stream, tag, "replace", 0, gainDb);
}

void MpvPlayer::beginFade()
{
    qCDebug(lcPlayer) << "fade begins over" << m_fadeSeconds << "s (in" << m_fadeInSeconds << "s), deck"
                      << m_fadeFrom + 1 << "to" << m_active + 1;
    m_fadeClock.start();
    m_fadeTimer.start();
    stepFade();
}

// equal-power curve: the sum of both stays as loud as one track (when both
// fades are as long; a shorter fade-in brings the new track up sooner)
void MpvPlayer::stepFade()
{
    if (m_fadeFrom < 0) {
        m_fadeTimer.stop();
        return;
    }
    // the curve stands still while paused
    static constexpr double kTick = 0.03;
    if (m_paused) {
        m_fadeClock.restart();
        return;
    }
    const double elapsed = qMin(m_fadeClock.elapsed() / 1000.0, kTick * 4);
    m_fadeClock.restart();
    m_fadeProgress = qMin(1.0, m_fadeProgress + elapsed / m_fadeSeconds);
    const double angle = m_fadeProgress * M_PI / 2;
    const double inAngle = qMin(1.0, m_fadeProgress * m_fadeSeconds / m_fadeInSeconds) * M_PI / 2;
    setDeckProperty(m_active, "volume", m_volume * std::sin(inAngle));
    setDeckProperty(m_fadeFrom, "volume", m_volume * std::cos(angle));
    if (m_fadeProgress >= 1.0)
        endFade();
}

void MpvPlayer::endFade()
{
    if (m_fadeFrom >= 0)
        qCDebug(lcPlayer) << "fade ends at" << m_fadeProgress;
    m_fadeTimer.stop();
    if (m_fadeFrom >= 0) {
        command(m_fadeFrom, QVariantList{QStringLiteral("stop")});
        setDeckProperty(m_fadeFrom, "volume", 0.0);
    }
    m_fadeFrom = -1;
    m_fadeProgress = 0;
    applyVolume();
}

// a load, seek or stop ends any crossfade at once
void MpvPlayer::abortCrossfade()
{
    if (!m_crossfadeTag.isEmpty()) {
        command(1 - m_active, QVariantList{QStringLiteral("stop")});
        m_crossfadeTag.clear();
    }
    if (m_fadeFrom >= 0)
        endFade();
}

void MpvPlayer::applyVolume()
{
    if (m_fadeFrom < 0)
        setDeckProperty(m_active, "volume", m_volume);
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
    setDeckProperty(m_active, "volume-gain", gainDb);
}

void MpvPlayer::setFilters(const QString &filters)
{
    if (filters == m_filters)
        return;
    m_filters = filters;
    setMpvProperty("af", filters);
}

void MpvPlayer::setVisualizer(const QString &graph)
{
    if (graph == m_visualizer)
        return;
    m_visualizer = graph;
    // the file playing gets it now, later files with their load
    if (m_videoOutput && !m_activeIsVideo)
        setDeckProperty(m_active, "lavfi-complex", graph);
}

void MpvPlayer::setVideoOutput(bool on)
{
    if (on == m_videoOutput)
        return;
    m_videoOutput = on;
    // the file playing: its picture (or the visualizer) on or off
    if (m_activeIsVideo) {
        if (on && !m_activePicture) {
            emit pictureNeedsReload();
        } else if (!on) {
            setDeckProperty(m_active, "vid", QStringLiteral("no"));
            m_activePicture = false;
        }
    }
    else if (!m_visualizer.isEmpty())
        setDeckProperty(m_active, "lavfi-complex", on ? m_visualizer : QString());
}

void MpvPlayer::setSubtitle(const QUrl &url)
{
    if (url.isEmpty()) {
        setDeckProperty(m_active, "sid", QStringLiteral("no"));
        return;
    }
    command(m_active, QVariantList{QStringLiteral("sub-add"), url.toString(QUrl::FullyEncoded), QStringLiteral("select")});
}

void MpvPlayer::setSpeed(double speed)
{
    m_speed = qBound(0.25, speed, 4.0);
    setMpvProperty("speed", m_speed * m_speedBoost);
}

void MpvPlayer::setSpeedBoost(double factor)
{
    if (factor == m_speedBoost)
        return;
    m_speedBoost = factor;
    setDeckProperty(m_active, "speed", m_speed * m_speedBoost);
}

void MpvPlayer::setAudioDevice(const QString &device)
{
    m_audioDeviceWanted = device.isEmpty() ? QStringLiteral("auto") : device;
    setMpvProperty("audio-device", m_audioDeviceWanted);
}

void MpvPlayer::watchAudioDevices()
{
    if (!m_decks[0].mpv || m_watchingDevices)
        return;
    m_watchingDevices = true;
    // the outputs are the same for both decks
    mpv_observe_property(m_decks[0].mpv, PropAudioDevices, "audio-device-list", MPV_FORMAT_NODE);
}

void MpvPlayer::clearQueued()
{
    Deck &deck = m_decks[m_active];
    deck.waitingNext.reset();
    // a file still starting would go too (see Deck::startingTag); nothing is queued then
    if (!deck.startingTag.isEmpty())
        return;
    // removes every entry except the one playing
    command(m_active, QVariantList{QStringLiteral("playlist-clear")});
}

void MpvPlayer::setPaused(bool paused)
{
    // both decks: a crossfade pauses as a whole
    setMpvProperty("pause", paused);
}

void MpvPlayer::setVolume(double volume)
{
    m_volume = qBound(0.0, volume, 100.0);
    emit volumeChanged();
    applyVolume();
}

void MpvPlayer::setMuted(bool muted)
{
    setMpvProperty("mute", muted);
}

void MpvPlayer::togglePause()
{
    setPaused(!m_paused);
}

void MpvPlayer::seek(double seconds)
{
    abortCrossfade();
    command(m_active, QVariantList{QStringLiteral("seek"), seconds, QStringLiteral("absolute")});
}

void MpvPlayer::stop()
{
    abortCrossfade();
    command(m_active, QVariantList{QStringLiteral("stop")});
}
