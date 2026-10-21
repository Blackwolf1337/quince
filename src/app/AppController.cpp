#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "integrations/Desktop.h"
#include "integrations/Discord.h"
#include "lyrics/Lyrics.h"
#ifdef QUINCE_DBUS
#include "integrations/GlobalShortcuts.h"
#include "integrations/Mpris.h"
#endif
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/StreamResolver.h"
#include "ytm/Account.h"
#include "ytm/AccountCookies.h"
#include "ytm/InnerTube.h"
#include "ytm/Parser.h"

#include <QJSEngine>
#include <QStandardPaths>
#include <QTimer>

AppController *AppController::s_instance = nullptr;

namespace {

// a play counts in the account's history after this much of it was heard
constexpr double kHistoryAfterSeconds = 10.0;

} // namespace

AppController::AppController(const AppOptions &options, QObject *parent)
    : QObject(parent)
    , m_player(new MpvPlayer(options.audioOutput, this))
    , m_resolver(new StreamResolver(this))
    , m_innerTube(new InnerTube(this))
    , m_playback(new Playback(m_player, m_resolver, m_innerTube, this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    m_settings = new Settings(this);
    applySettings();
    connect(m_settings, &Settings::changed, this, &AppController::applySettings);

    m_cookies = new AccountCookies(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/cookies.txt"), this);
    m_innerTube->setCookies(m_cookies);
    m_resolver->setCookies(m_cookies);
    m_account = new Account(m_cookies, m_innerTube, this);

    connect(m_account, &Account::rated, m_playback, &Playback::setLikeStatus);

    m_theme = new ColorTheme(m_playback, m_settings, this);
    m_lyrics = new Lyrics(m_playback, m_player, m_innerTube, m_settings, this);
    m_desktop = new Desktop(m_playback, m_player, m_settings, this);
    m_discord = new Discord(m_playback, m_player, m_settings, this);
    connect(m_theme, &ColorTheme::coverReady, m_desktop, &Desktop::setCover);
#ifdef QUINCE_DBUS
    m_mpris = new Mpris(m_playback, m_player, m_settings, this);
    if (m_settings->globalShortcuts()) {
        auto *shortcuts = new GlobalShortcuts(this);
        shortcuts->add(QStringLiteral("play-pause"), tr("Play or pause"), [this] { m_player->togglePause(); });
        shortcuts->add(QStringLiteral("next"), tr("Next track"), [this] { m_playback->next(); });
        shortcuts->add(QStringLiteral("previous"), tr("Previous track"), [this] { m_playback->previous(); });
        shortcuts->add(QStringLiteral("volume-up"), tr("Volume up"), [this] {
            m_settings->setVolume(m_settings->volume() + m_settings->volumeStep());
        });
        shortcuts->add(QStringLiteral("volume-down"), tr("Volume down"), [this] {
            m_settings->setVolume(m_settings->volume() - m_settings->volumeStep());
        });
        shortcuts->add(QStringLiteral("like"), tr("Like or unlike the track"), [this] {
            const QString status = m_playback->track().value(QStringLiteral("likeStatus")).toString();
            m_account->rate(m_playback->videoId(), status == u"LIKE" ? QStringLiteral("INDIFFERENT")
                                                                     : QStringLiteral("LIKE"));
        });
        shortcuts->add(QStringLiteral("show"), tr("Show Quince"), [this] { m_mpris->raise(); });
        shortcuts->start();
    }
#endif
    connect(m_player, &MpvPlayer::positionChanged, this, [this] {
        const QString videoId = m_playback->videoId();
        if (videoId.isEmpty() || videoId == m_reportedPlay || m_player->position() < kHistoryAfterSeconds)
            return;
        // the player answer with the report URL may still be on its way
        const QString url = m_playback->trackingUrl(videoId);
        if (url.isEmpty())
            return;
        m_reportedPlay = videoId;
        m_account->reportPlay(videoId, url);
    });
    // a new track may be the same song again
    connect(m_playback, &Playback::trackChanged, this, [this] {
        if (m_playback->videoId() != m_reportedPlay)
            m_reportedPlay.clear();
    });

    if (!options.playOnStart.isEmpty()) {
        const QString text = options.playOnStart;
        QTimer::singleShot(0, this, [this, text] { m_playback->playText(text); });
    } else if (options.restoreSession) {
        QTimer::singleShot(0, m_playback, &Playback::restoreSession);
    }
}

AppController::~AppController()
{
    // the session needs the player's position
    m_playback->saveSession();
    delete m_playback;
    s_instance = nullptr;
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT(s_instance);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void AppController::open(const QVariantMap &item)
{
    if (!item.value(QStringLiteral("videoId")).toString().isEmpty()) {
        m_playback->play(item);
        return;
    }
    const QString browseId = item.value(QStringLiteral("browseId")).toString();
    if (!browseId.isEmpty()) {
        openBrowse(browseId, item.value(QStringLiteral("params")).toString());
        return;
    }
    // a mix without a page of its own
    if (item.value(QStringLiteral("kind")).toString() == u"station")
        m_playback->play(item);
}

void AppController::openBrowse(const QString &browseId, const QString &params)
{
    emit navigateRequested({{QStringLiteral("browseId"), browseId}, {QStringLiteral("params"), params}});
}

void AppController::setWindow(QWindow *window)
{
    m_desktop->setWindow(window);
#ifdef QUINCE_DBUS
    m_mpris->setWindow(window);
#endif
}

void AppController::applySettings()
{
    m_player->setVolume(m_settings->toPlayerVolume(m_settings->volume()));
    if (m_player->audioDevice() != m_settings->audioDevice())
        m_player->setAudioDevice(m_settings->audioDevice());
    m_playback->setNormalizeLoudness(m_settings->normalizeLoudness());
}

void AppController::setVolumeSlider(double slider)
{
    m_settings->setVolume(slider);
}

void AppController::requestDialog(const QString &name, const QVariantMap &data)
{
    emit dialogRequested(name, data);
}

QString AppController::art(const QString &url, int pixels) const
{
    return ytm::artUrl(url, pixels);
}
