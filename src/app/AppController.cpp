#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Language.h"
#include "app/Settings.h"
#include "integrations/Desktop.h"
#include "integrations/Discord.h"
#include "integrations/SponsorBlock.h"
#include "lyrics/Lyrics.h"
#ifdef QUINCE_DBUS
#include "integrations/GlobalShortcuts.h"
#include "integrations/Mpris.h"
#endif
#include "download/Downloader.h"
#include "player/AudioEffects.h"
#include "player/JsSolver.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/StreamResolver.h"
#include "ytm/Account.h"
#include "ytm/AccountCookies.h"
#include "ytm/InnerTube.h"
#include "ytm/Parser.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJSEngine>
#include <QStandardPaths>
#include <QTimer>

AppController *AppController::s_instance = nullptr;

namespace {

// a play counts in the account's history after this much of it was heard
constexpr double kHistoryAfterSeconds = 10.0;

// lavfi graphs from the sound to a picture ([vo]) for each visualizer style
QString visualizerGraph(const QString &style)
{
    QString draw;
    if (style == u"bars")
        draw = QStringLiteral("showfreqs=s=1280x720:mode=bar:ascale=log:fscale=log:win_size=2048:colors=0xe8e8f0");
    else if (style == u"wave")
        draw = QStringLiteral("showwaves=s=1280x720:mode=cline:rate=30:scale=sqrt:colors=0xe8e8f0");
    else if (style == u"spectrum")
        draw = QStringLiteral("showcqt=s=1280x720:fps=30:sono_h=0:axis=0:bar_g=2");
    else if (style == u"scope")
        draw = QStringLiteral("avectorscope=s=720x720:r=30:zoom=1.3:draw=line:rc=230:gc=230:bc=240:rf=12:gf=12:bf=10");
    else
        return {};
    // black turns see-through, so the picture lies on the view's own light
    return QStringLiteral("[aid1]asplit[ao][a];[a]%1,format=rgba,colorkey=0x000000:0.08:0.2[vo]").arg(draw);
}

} // namespace

AppController::AppController(const AppOptions &options, QObject *parent)
    : QObject(parent)
    , m_player(new MpvPlayer(options.audioOutput, this))
    , m_innerTube(new InnerTube(this))
    , m_solver(new JsSolver(m_innerTube, this))
    , m_resolver(new StreamResolver(m_innerTube, m_solver, this))
    , m_playback(new Playback(m_player, m_resolver, m_innerTube, this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    m_settings = new Settings(this);
    // before anything keeps a translated text
    auto *language = new Language(m_settings, this);
    m_innerTube->setLanguage(language->code());
    applySettings();
    connect(m_settings, &Settings::changed, this, &AppController::applySettings);

    m_cookies = new AccountCookies(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/cookies.txt"), this);
    m_innerTube->setCookies(m_cookies);
    m_resolver->setCookies(m_cookies);
    m_account = new Account(m_cookies, m_innerTube, this);
    // Premium streams and tracks VISIONOS refuses need YouTube's player JS
    // solved; a new player is solved in the background once
    m_solver->prepare();

    connect(m_account, &Account::rated, m_playback, &Playback::setLikeStatus);

    m_theme = new ColorTheme(m_playback, m_settings, this);
    m_lyrics = new Lyrics(m_playback, m_player, m_innerTube, m_settings, this);
    m_effects = new AudioEffects(m_player, m_playback, m_settings, this);
    m_downloads = new Downloader(m_resolver, m_innerTube, m_settings, this);
    m_sponsorBlock = new SponsorBlock(m_playback, m_player, m_settings, this);
    m_desktop = new Desktop(m_playback, m_player, m_settings, this);
    m_discord = new Discord(m_playback, m_player, m_settings, this);
    connect(m_theme, &ColorTheme::coverReady, m_desktop, &Desktop::setCover);
    connect(language, &Language::changed, this, [this, language] {
        // pages loaded from now on come in the new language
        m_innerTube->setLanguage(language->code());
        m_player->retranslate();
        m_desktop->retranslate();
        // the preset name comes from a call, not a qsTr() binding
        emit m_effects->changed();
        emit languageChanged();
    });
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
    } else if (options.restoreSession && m_settings->resumeSession()) {
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
    m_playback->setSkipDisliked(m_settings->skipDisliked());
    m_playback->setStartPaused(m_settings->startPaused());
    m_playback->setCaptionLanguage(m_settings->captionLanguage());
    m_playback->setVideoHeight(m_settings->videoHeight());
    m_playback->setVideoMode(m_settings->videoMode());
    m_player->setVisualizer(visualizerGraph(m_settings->visualizer()));
}

void AppController::setVolumeSlider(double slider)
{
    m_settings->setVolume(slider);
}

void AppController::requestDialog(const QString &name, const QVariantMap &data)
{
    emit dialogRequested(name, data);
}

void AppController::copyLink(const QVariantMap &item)
{
    const QString base = QStringLiteral("https://music.youtube.com/");
    const QString videoId = item.value(QStringLiteral("videoId")).toString();
    const QString browseId = item.value(QStringLiteral("browseId")).toString();
    QString link;
    if (!videoId.isEmpty())
        link = base + QStringLiteral("watch?v=") + videoId;
    else if (browseId.startsWith(u"VL"))
        link = base + QStringLiteral("playlist?list=") + browseId.mid(2);
    else if (browseId.startsWith(u"UC"))
        link = base + QStringLiteral("channel/") + browseId;
    else if (!browseId.isEmpty())
        link = base + QStringLiteral("browse/") + browseId;
    if (link.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(link);
    emit message(tr("Link copied"));
}

QString AppController::art(const QString &url, int pixels) const
{
    return ytm::artUrl(url, pixels);
}
