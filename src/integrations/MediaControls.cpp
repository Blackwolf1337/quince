#include "integrations/MediaControls.h"
#include "app/UiHost.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "ytm/Parser.h"

#include <QStringList>

#include <cmath>

namespace {

// the cover in the system's controls
constexpr int kArtPixels = 512;
// a position this far from where playing on would be is a seek
constexpr double kSeekSeconds = 1.5;

} // namespace

MediaControls::MediaControls(Playback *playback, MpvPlayer *player, UiHost *host, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_host(host)
{
    connect(m_playback, &Playback::trackChanged, this, &MediaControls::update);
    connect(m_playback, &Playback::queueStateChanged, this, &MediaControls::update);
    connect(m_player, &MpvPlayer::pausedChanged, this, &MediaControls::update);
    connect(m_player, &MpvPlayer::idleChanged, this, &MediaControls::update);
    connect(m_player, &MpvPlayer::durationChanged, this, &MediaControls::update);
    connect(m_player, &MpvPlayer::positionChanged, this, &MediaControls::positionMoved);
}

MediaControls::~MediaControls()
{
    if (m_system)
        close(m_system);
}

void MediaControls::pressed(Button button)
{
    QMetaObject::invokeMethod(this, [this, button] {
        switch (button) {
        case Button::Play:
            m_player->setPaused(false);
            break;
        case Button::Pause:
        case Button::Stop:
            m_player->setPaused(true);
            break;
        case Button::Toggle:
            m_player->togglePause();
            break;
        case Button::Next:
            m_playback->next();
            break;
        case Button::Previous:
            m_playback->previous();
            break;
        }
    }, Qt::QueuedConnection);
}

void MediaControls::seekTo(double seconds)
{
    QMetaObject::invokeMethod(this, [this, seconds] { m_player->seek(qMax(0.0, seconds)); }, Qt::QueuedConnection);
}

void MediaControls::update()
{
    if (!m_system)
        m_system = open(this, m_host ? m_host->nativeWindow() : nullptr);
    if (!m_system)
        return;
    const QVariantMap track = m_playback->track();
    State state;
    state.hasTrack = !track.value(QStringLiteral("videoId")).toString().isEmpty();
    state.playing = state.hasTrack && !m_player->paused() && !m_player->idle();
    state.canNext = m_playback->hasNext();
    state.canPrevious = m_playback->hasPrevious();
    state.title = track.value(QStringLiteral("title")).toString();
    state.artist = ytm::artistNames(track).join(QStringLiteral(", "));
    state.album = track.value(QStringLiteral("album")).toMap().value(QStringLiteral("name")).toString();
    const QString art = track.value(QStringLiteral("thumbnail")).toString();
    state.artUrl = art.isEmpty() ? QString() : ytm::artUrl(art, kArtPixels);
    state.duration = m_player->duration() > 0 ? m_player->duration() : track.value(QStringLiteral("durationSeconds")).toDouble();
    state.position = m_player->position();
    show(m_system, state);
    m_shownPosition = state.position;
    m_sinceShown.restart();
}

void MediaControls::positionMoved()
{
    const bool playing = !m_player->paused() && !m_player->idle();
    const double expected = m_shownPosition + (playing ? m_sinceShown.elapsed() / 1000.0 : 0);
    if (m_system && std::abs(m_player->position() - expected) > kSeekSeconds)
        update();
}
