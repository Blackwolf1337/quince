#include "integrations/Mpris.h"
#include "app/Settings.h"
#include "app/UiHost.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "ytm/Parser.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <cmath>

Q_LOGGING_CATEGORY(lcMpris, "quince.mpris")

namespace {

const QString kService = QStringLiteral("org.mpris.MediaPlayer2.quince");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
constexpr double kMicro = 1e6;
// a position jump larger than this is a seek
constexpr double kSeekThreshold = 1.5;
constexpr int kArtPixels = 512;

// object paths allow [A-Za-z0-9_] only; video ids also have "-"
QDBusObjectPath trackPath(const QString &videoId)
{
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_]"));
    QString id = videoId;
    id.replace(invalid, QStringLiteral("_"));
    return QDBusObjectPath(QStringLiteral("/org/quince/track/") + (id.isEmpty() ? QStringLiteral("none") : id));
}

} // namespace

Mpris::Mpris(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
    , m_host(host)
{
    new MprisRoot(this);
    new MprisPlayer(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcMpris) << "no session bus";
        return;
    }
    // a second instance gets a unique suffix, as the spec allows
    QString service = kService;
    if (!bus.registerService(service)) {
        service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
        if (!bus.registerService(service)) {
            qCWarning(lcMpris) << "could not register" << service;
            return;
        }
    }
    bus.registerObject(kPath, this);
    m_registered = true;

    connect(m_playback, &Playback::trackChanged, this, &Mpris::onTrackChanged);
    connect(m_playback, &Playback::queueStateChanged, this, [this] {
        propertiesChanged(kPlayerInterface, {{QStringLiteral("CanGoNext"), canGoNext()},
                                             {QStringLiteral("CanGoPrevious"), canGoPrevious()}});
    });
    auto status = [this] {
        propertiesChanged(kPlayerInterface, {{QStringLiteral("PlaybackStatus"), playbackStatus()},
                                             {QStringLiteral("CanPlay"), canPlay()},
                                             {QStringLiteral("CanPause"), canPlay()},
                                             {QStringLiteral("CanSeek"), canSeek()}});
    };
    connect(m_player, &MpvPlayer::pausedChanged, this, status);
    connect(m_player, &MpvPlayer::idleChanged, this, status);
    connect(m_player, &MpvPlayer::durationChanged, this, [this] {
        propertiesChanged(kPlayerInterface, {{QStringLiteral("Metadata"), metadata()},
                                             {QStringLiteral("CanSeek"), canSeek()}});
    });
    connect(m_settings, &Settings::changed, this, [this] {
        propertiesChanged(kPlayerInterface, {{QStringLiteral("Volume"), volume()}});
    });
    connect(m_player, &MpvPlayer::positionChanged, this, &Mpris::onPositionChanged);
}

Mpris::~Mpris()
{
    if (m_registered)
        QDBusConnection::sessionBus().unregisterObject(kPath);
}

QString Mpris::playbackStatus() const
{
    if (m_player->idle())
        return QStringLiteral("Stopped");
    return m_player->paused() ? QStringLiteral("Paused") : QStringLiteral("Playing");
}

QVariantMap Mpris::metadata() const
{
    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    if (videoId.isEmpty())
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath({}))}};

    const QStringList artists = ytm::artistNames(track);

    double length = m_player->duration();
    if (length <= 0)
        length = track.value(QStringLiteral("durationSeconds")).toDouble();

    QVariantMap map{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath(videoId))},
        {QStringLiteral("xesam:title"), track.value(QStringLiteral("title")).toString()},
        {QStringLiteral("xesam:artist"), artists},
        {QStringLiteral("xesam:url"), QStringLiteral("https://music.youtube.com/watch?v=") + videoId},
    };
    if (length > 0)
        map.insert(QStringLiteral("mpris:length"), static_cast<qlonglong>(length * kMicro));
    const QString album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    if (!album.isEmpty())
        map.insert(QStringLiteral("xesam:album"), album);
    const QString art = track.value(QStringLiteral("thumbnail")).toString();
    if (!art.isEmpty())
        map.insert(QStringLiteral("mpris:artUrl"), ytm::artUrl(art, kArtPixels));
    if (track.value(QStringLiteral("likeStatus")).toString() == u"LIKE")
        map.insert(QStringLiteral("xesam:userRating"), 1.0);
    return map;
}

// the slider position, the same scale the volume slider shows
double Mpris::volume() const
{
    return m_settings->volume() / 100.0;
}

void Mpris::setVolume(double volume)
{
    m_settings->setVolume(qBound(0.0, volume, 1.0) * 100.0);
}

qlonglong Mpris::position() const
{
    return static_cast<qlonglong>(m_player->position() * kMicro);
}

bool Mpris::canGoNext() const
{
    return m_playback->hasNext();
}

// "previous" always works while a track is there: it goes back to its start
bool Mpris::canGoPrevious() const
{
    return !m_playback->videoId().isEmpty();
}

bool Mpris::canPlay() const
{
    return !m_playback->videoId().isEmpty();
}

bool Mpris::canSeek() const
{
    return m_player->duration() > 0;
}

void Mpris::raise()
{
    if (m_host)
        m_host->raiseWindow();
}

void Mpris::propertiesChanged(const QString &interface, const QVariantMap &changed)
{
    if (!m_registered)
        return;
    QDBusMessage signal = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                     QStringLiteral("PropertiesChanged"));
    signal << interface << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

void Mpris::onTrackChanged()
{
    propertiesChanged(kPlayerInterface, {{QStringLiteral("Metadata"), metadata()},
                                         {QStringLiteral("CanPlay"), canPlay()},
                                         {QStringLiteral("CanPause"), canPlay()},
                                         {QStringLiteral("CanGoNext"), canGoNext()},
                                         {QStringLiteral("CanGoPrevious"), canGoPrevious()}});
}

// Position is not announced on every change; a jump is a seek and gets the
// Seeked signal.
void Mpris::onPositionChanged()
{
    const double position = m_player->position();
    const double expected = m_lastPosition;
    m_lastPosition = position;
    if (std::abs(position - expected) <= kSeekThreshold)
        return;
    if (auto *adaptor = findChild<MprisPlayer *>())
        emit adaptor->Seeked(static_cast<qlonglong>(position * kMicro));
}

MprisRoot::MprisRoot(Mpris *mpris)
    : QDBusAbstractAdaptor(mpris)
    , m_mpris(mpris)
{
}

void MprisRoot::Raise()
{
    m_mpris->raise();
}

void MprisRoot::Quit()
{
    QCoreApplication::quit();
}

MprisPlayer::MprisPlayer(Mpris *mpris)
    : QDBusAbstractAdaptor(mpris)
    , m_mpris(mpris)
{
}

void MprisPlayer::Next()
{
    m_mpris->playback()->next();
}

void MprisPlayer::Previous()
{
    m_mpris->playback()->previous();
}

void MprisPlayer::Pause()
{
    m_mpris->player()->setPaused(true);
}

void MprisPlayer::PlayPause()
{
    m_mpris->player()->togglePause();
}

void MprisPlayer::Stop()
{
    m_mpris->player()->setPaused(true);
}

void MprisPlayer::Play()
{
    m_mpris->player()->setPaused(false);
}

void MprisPlayer::Seek(qlonglong offset)
{
    const double target = m_mpris->player()->position() + offset / kMicro;
    if (target < 0) {
        m_mpris->player()->seek(0);
    } else if (target > m_mpris->player()->duration()) {
        m_mpris->playback()->next();
    } else {
        m_mpris->player()->seek(target);
    }
}

void MprisPlayer::SetPosition(const QDBusObjectPath &trackId, qlonglong position)
{
    // the spec ignores requests for a track that is not playing anymore
    if (trackId.path() != trackPath(m_mpris->playback()->videoId()).path())
        return;
    const double seconds = position / kMicro;
    if (seconds < 0 || seconds > m_mpris->player()->duration())
        return;
    m_mpris->player()->seek(seconds);
}

void MprisPlayer::OpenUri(const QString &uri)
{
    m_mpris->playback()->playText(uri);
}
