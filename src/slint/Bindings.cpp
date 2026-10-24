#include "Bindings.h"
#include "Areas.h"
#include "ImageCache.h"
#include "ModelMirror.h"
#include "SlintHost.h"
#include "VideoRenderer.h"
#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "download/Downloader.h"
#include "integrations/Notifications.h"
#include "integrations/SponsorBlock.h"
#include "player/AudioEffects.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/Queue.h"
#include "ytm/Account.h"

#include <QColor>
#include <QHash>
#include <QCoreApplication>
#include <QLocale>
#include <QTime>
#include <QTimer>

using namespace bridge;

namespace {

// cover sizes in logical pixels: the big one (the player bar shows it too),
// queue rows, the account's photo
constexpr int kBigArt = 320;
constexpr int kRowArt = 44;
constexpr int kPhoto = 30;
constexpr int kClockMs = 1000;

struct QueueData
{
    std::string title;
    std::string subtitle;
    std::string artUrl;
    std::string duration;
    std::string kind;
    bool explicitContent = false;
    bool unavailable = false;
    bool current = false;
};

struct TrackData
{
    std::string title;
    std::string subtitle;
    std::string artUrl;
    std::string videoId;
    std::string likeStatus;
};

struct PlaylistData
{
    std::string title;
    std::string browseId;
};

TrackData trackOf(const QVariantMap &track)
{
    return { text(track, "title"), utf8(Notifications::artistsOf(track)), text(track, "thumbnail"),
             text(track, "videoId"), text(track, "likeStatus") };
}

slint::Color color(const QColor &c)
{
    return slint::Color::from_argb_uint8(uint8_t(c.alpha()), uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()));
}

// the clock's text in the format the settings ask for; empty while it is off
std::string clockText(const Settings *settings)
{
    if (!settings->clock())
        return {};
    const bool seconds = settings->clockSeconds();
    QString format;
    if (settings->clockHours() == u"24")
        format = seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm");
    else if (settings->clockHours() == u"12")
        format = seconds ? QStringLiteral("h:mm:ss AP") : QStringLiteral("h:mm AP");
    else {
        format = QLocale::system().timeFormat(QLocale::ShortFormat);
        if (seconds)
            format.replace(QStringLiteral("mm"), QStringLiteral("mm:ss"));
    }
    return utf8(QTime::currentTime().toString(format));
}

void bindPlayer(AppController &app, const Window &window, ImageCache *images)
{
    MpvPlayer *player = app.player();
    Playback *playback = app.playback();
    Settings *settings = app.settings();

    bind(window, player, &MpvPlayer::pausedChanged, [](MpvPlayer *p) { return p->paused(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_paused(v); });
    bind(window, player, &MpvPlayer::idleChanged, [](MpvPlayer *p) { return p->idle(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_idle(v); });
    bind(window, player, &MpvPlayer::bufferingChanged, [](MpvPlayer *p) { return p->buffering(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_buffering(v); });
    bind(window, player, &MpvPlayer::positionChanged, [](MpvPlayer *p) { return float(p->position()); },
         [](ui::MainWindow &w, float v) { w.global<ui::Player>().set_position(v); });
    bind(window, player, &MpvPlayer::durationChanged, [](MpvPlayer *p) { return float(p->duration()); },
         [](ui::MainWindow &w, float v) { w.global<ui::Player>().set_duration(v); });
    bind(window, player, &MpvPlayer::mutedChanged, [](MpvPlayer *p) { return p->muted(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_muted(v); });
    bind(window, settings, &Settings::changed, [](Settings *s) { return float(s->volume()); },
         [](ui::MainWindow &w, float v) { w.global<ui::Player>().set_volume(v); });
    bind(window, settings, &Settings::changed, [](Settings *s) { return float(s->volumeStep()); },
         [](ui::MainWindow &w, float v) { w.global<ui::Player>().set_volume_step(v); });
    bind(window, app.effects(), &AudioEffects::changed, [](AudioEffects *e) { return float(e->speed()); },
         [](ui::MainWindow &w, float v) { w.global<ui::Player>().set_speed(v); });

    bind(window, playback, &Playback::loadingChanged, [](Playback *p) { return p->loading(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_loading(v); });
    bind(window, playback, &Playback::errorChanged, [](Playback *p) { return utf8(p->error()); },
         [](ui::MainWindow &w, const std::string &v) { w.global<ui::Player>().set_error(str(v)); });
    bind(window, playback, &Playback::queueStateChanged, [](Playback *p) { return p->hasNext(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_has_next(v); });
    bind(window, playback, &Playback::streamInfoChanged, [](Playback *p) { return p->videoPlaying(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_video_playing(v); });
    bind(window, playback, &Playback::streamInfoChanged, [](Playback *p) { return utf8(p->streamInfo()); },
         [](ui::MainWindow &w, const std::string &v) { w.global<ui::Player>().set_stream_info(str(v)); });
    bind(window, playback->queue(), &Queue::currentChanged, [](Queue *q) { return q->current(); },
         [](ui::MainWindow &w, int v) { w.global<ui::Player>().set_queue_current(v); });

    auto shownArt = std::make_shared<std::string>(); // the cover asked for last (Slint thread only)
    bind(window, playback, &Playback::trackChanged, [](Playback *p) { return trackOf(p->track()); },
         [images, window, shownArt](ui::MainWindow &w, const TrackData &t) {
        auto &p = w.global<ui::Player>();
        p.set_title(str(t.title));
        p.set_subtitle(str(t.subtitle));
        p.set_video_id(str(t.videoId));
        p.set_like_status(str(t.likeStatus));
        if (*shownArt == t.artUrl)
            return;
        *shownArt = t.artUrl;
        const int pixels = int(kBigArt * w.window().scale_factor());
        images->get(t.artUrl, pixels, [window, shownArt, url = t.artUrl](const slint::Image &image) {
            auto ui = window.lock();
            // a later track may have asked for another cover meanwhile
            if (ui && *shownArt == url)
                (*ui)->global<ui::Player>().set_art(image);
        });
    });

    ModelMirror<QueueData, ui::ListItem>::start(
        window, playback->queue(),
        [](const QModelIndex &index) {
            const QVariantMap item = index.data(Queue::ItemRole).toMap();
            return QueueData{ text(item, "title"), utf8(Notifications::artistsOf(item)), text(item, "thumbnail"),
                              text(item, "duration"), text(item, "kind"),
                              item.value(QStringLiteral("explicit")).toBool(),
                              item.value(QStringLiteral("unavailable")).toBool(),
                              index.data(Queue::CurrentRole).toBool() };
        },
        [](const QueueData &d) {
            ui::ListItem row;
            row.title = str(d.title);
            row.subtitle = str(d.subtitle);
            row.art_url = str(d.artUrl);
            row.duration = str(d.duration);
            row.kind = str(d.kind);
            row.explicit_ = d.explicitContent;
            row.unavailable = d.unavailable;
            row.current = d.current;
            return row;
        },
        [](ui::MainWindow &w, std::shared_ptr<slint::VectorModel<ui::ListItem>> model) { w.global<ui::Player>().set_queue(model); },
        [images](const std::shared_ptr<slint::VectorModel<ui::ListItem>> &model, size_t, const QueueData &d) {
            // rows move while the picture loads: it goes to every row with this cover
            images->get(d.artUrl, kRowArt * 2, [weak = std::weak_ptr(model), url = str(d.artUrl)](const slint::Image &image) {
                auto m = weak.lock();
                if (!m)
                    return;
                for (size_t i = 0; i < m->row_count(); i++) {
                    ui::ListItem row = *m->row_data(i);
                    if (row.art_url == url) {
                        row.art = image;
                        m->set_row_data(i, row);
                    }
                }
            });
        });
}

void registerQueueItems(Queue *queue)
{
    // a: the row in the queue
    registerItemSource(QStringLiteral("queue"), [queue](int row, int) {
        return queue->index(row, 0).data(Queue::ItemRole).toMap();
    });
}

void bindAccount(Account *account, const Window &window, ImageCache *images)
{
    bind(window, account, &Account::signedInChanged, [](Account *a) { return a->signedIn(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Account>().set_signed_in(v); });
    struct Info
    {
        std::string name, handle, photo;
    };
    bind(window, account, &Account::infoChanged,
         [](Account *a) { return Info{ utf8(a->name()), utf8(a->handle()), utf8(a->photo()) }; },
         [images, window](ui::MainWindow &w, const Info &info) {
        auto &a = w.global<ui::Account>();
        a.set_name(str(info.name));
        a.set_handle(str(info.handle));
        const int pixels = int(kPhoto * w.window().scale_factor());
        images->get(info.photo, pixels, [window](const slint::Image &image) {
            if (auto ui = window.lock())
                (*ui)->global<ui::Account>().set_photo(image);
        });
    });
    bind(window, account, &Account::playlistsChanged,
         [](Account *a) {
        std::vector<PlaylistData> list;
        for (const QVariant &v : a->playlists()) {
            const QVariantMap p = v.toMap();
            list.push_back({ text(p, "title"), text(p, "browseId") });
        }
        return list;
    },
         [](ui::MainWindow &w, const std::vector<PlaylistData> &list) {
        auto model = std::make_shared<slint::VectorModel<ui::PlaylistEntry>>();
        for (const PlaylistData &p : list) {
            ui::PlaylistEntry entry;
            entry.title = str(p.title);
            entry.browse_id = str(p.browseId);
            model->push_back(entry);
        }
        w.global<ui::Account>().set_playlists(model);
    });
}

void bindApp(AppController &app, const Window &window)
{
    Settings *settings = app.settings();
    bind(window, settings, &Settings::changed, [](Settings *s) { return s->compactSidebar(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::App>().set_compact_sidebar(v); });
    bind(window, settings, &Settings::changed, [](Settings *s) { return s->coloredProgress(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::App>().set_colored_progress(v); });
    bind(window, settings, &Settings::changed, [](Settings *s) { return s->videoMode(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::App>().set_video_mode(v); });
    bind(window, app.downloads(), &Downloader::runningChanged, [](Downloader *d) { return d->running(); },
         [](ui::MainWindow &w, int v) { w.global<ui::App>().set_downloads_running(v); });

    // the clock: every second while it is on, and when the settings change
    auto *clock = new QTimer(settings);
    clock->setInterval(kClockMs);
    auto tick = [window, settings] {
        toUi(window, [t = clockText(settings)](ui::MainWindow &w) { w.global<ui::App>().set_clock(str(t)); });
    };
    QObject::connect(clock, &QTimer::timeout, settings, tick);
    QObject::connect(settings, &Settings::changed, clock, [clock, settings, tick] {
        settings->clock() ? clock->start() : clock->stop();
        tick();
    });
    if (settings->clock())
        clock->start();
    tick();

    // short messages for the toast; playback errors in red
    auto toast = [window](const QString &message, bool error) {
        toUi(window, [m = utf8(message), error](ui::MainWindow &w) {
            auto &a = w.global<ui::App>();
            a.set_toast(str(m));
            a.set_toast_error(error);
            a.set_toast_serial(a.get_toast_serial() + 1);
        });
    };
    QObject::connect(&app, &AppController::message, &app, [toast](const QString &m) { toast(m, false); });
    QObject::connect(app.account(), &Account::message, &app, [toast](const QString &m) { toast(m, false); });
    QObject::connect(app.downloads(), &Downloader::message, &app, [toast](const QString &m) { toast(m, false); });
    QObject::connect(app.sponsorBlock(), &SponsorBlock::skipped, &app, [toast](const QString &m) { toast(m, false); });
    QObject::connect(app.playback(), &Playback::errorChanged, &app, [toast, playback = app.playback()] {
        if (!playback->error().isEmpty())
            toast(playback->error(), true);
    });
}

void bindTheme(ColorTheme *theme, const Window &window)
{
    struct Colors
    {
        QColor accent, background, surface, surfaceHigh;
    };
    bind(window, theme, &ColorTheme::colorsChanged,
         [](ColorTheme *t) { return Colors{ t->accent(), t->background(), t->surface(), t->surfaceHigh() }; },
         [](ui::MainWindow &w, const Colors &c) {
        auto &style = w.global<ui::Style>();
        style.set_accent(color(c.accent));
        style.set_background(color(c.background));
        style.set_surface(color(c.surface));
        style.set_surface_high(color(c.surfaceHigh));
    });
}

void bindTray(MpvPlayer *player, Playback *playback, const Tray &tray)
{
    bind(tray, playback, &Playback::trackChanged,
         [](Playback *p) {
        const TrackData t = trackOf(p->track());
        return t.title.empty() ? utf8(QCoreApplication::translate("Tray", "Nothing playing"))
                               : t.title + " - " + t.subtitle;
    },
         [](ui::TrayIcon &t, const std::string &line) { t.set_now_playing(str(line)); });
    bind(tray, player, &MpvPlayer::pausedChanged, [](MpvPlayer *p) { return !p->paused() && !p->idle(); },
         [](ui::TrayIcon &t, bool playing) { t.set_playing(playing); });
}

// the Slint callbacks: they run on the Slint thread and post to the core
void connectCallbacks(AppController *app, const Window &window, const Tray &tray, SlintHost *host)
{
    MpvPlayer *player = app->player();
    Playback *playback = app->playback();
    Settings *settings = app->settings();
    AudioEffects *effects = app->effects();
    Account *account = app->account();
    toUi(window, [=](ui::MainWindow &w) {
        auto &p = w.global<ui::Player>();
        p.on_toggle_pause([player] { toCore(player, [player] { player->togglePause(); }); });
        p.on_next([playback] { toCore(playback, [playback] { playback->next(); }); });
        p.on_previous([playback] { toCore(playback, [playback] { playback->previous(); }); });
        p.on_seek([player](float s) { toCore(player, [player, s] { player->seek(s); }); });
        p.on_set_volume([app](float v) { toCore(app, [app, v] { app->setVolumeSlider(v); }); });
        p.on_set_muted([player](bool m) { toCore(player, [player, m] { player->setMuted(m); }); });
        p.on_set_speed([effects](float s) { toCore(effects, [effects, s] { effects->setSpeed(s); }); });
        p.on_play_index([playback](int i) { toCore(playback, [playback, i] { playback->playIndex(i); }); });
        p.on_play_text([playback](const slint::SharedString &t) {
            toCore(playback, [playback, t = fromSlint(t)] { playback->playText(t); });
        });
        p.on_rate([account, playback](const slint::SharedString &status) {
            toCore(account, [account, playback, s = fromSlint(status)] { account->rate(playback->videoId(), s); });
        });
        p.on_shuffle_rest([playback] { toCore(playback, [playback] { playback->queue()->shuffleRest(); }); });
        p.on_clear_rest([playback] { toCore(playback, [playback] { playback->queue()->clearRest(); }); });

        auto &a = w.global<ui::Account>();
        a.on_sign_in([account] { toCore(account, [account] { account->signIn(); }); });
        a.on_sign_out([account] { toCore(account, [account] { account->signOut(); }); });

        auto &appState = w.global<ui::App>();
        appState.on_set_video_mode([settings](bool on) { toCore(settings, [settings, on] { settings->setVideoMode(on); }); });
        appState.on_request_dialog([app](const slint::SharedString &name) {
            toCore(app, [app, n = fromSlint(name)] { app->requestDialog(n); });
        });
    });
    toUi(tray, [player, playback, host](ui::TrayIcon &t) {
        t.on_toggle_pause([player] { toCore(player, [player] { player->togglePause(); }); });
        t.on_next([playback] { toCore(playback, [playback] { playback->next(); }); });
        t.on_previous([playback] { toCore(playback, [playback] { playback->previous(); }); });
        t.on_toggle_window([host] { host->toggleWindow(); });
        t.on_quit([] { slint::quit_event_loop(); });
    });
}

void bindVideo(MpvPlayer *player, const Window &window, VideoRenderer *video)
{
    // the renderer only knows the deck's mpv handle; the handle is thread safe
    auto source = [player, window, video] {
        toUi(window, [video, mpv = player->renderHandle()](ui::MainWindow &) { video->setSource(mpv); });
    };
    QObject::connect(player, &MpvPlayer::activeDeckChanged, player, source);
    source();
    toUi(window, [player, video](ui::MainWindow &) {
        video->onOutput = [player](bool on) { toCore(player, [player, on] { player->setVideoOutput(on); }); };
    });
}

} // namespace

namespace {
QHash<QString, ItemLookup> s_itemSources; // core thread only
} // namespace

void registerItemSource(const QString &source, ItemLookup lookup)
{
    s_itemSources.insert(source, std::move(lookup));
}

QVariantMap lookupItem(const QString &source, int a, int b)
{
    const auto it = s_itemSources.constFind(source);
    return it == s_itemSources.cend() ? QVariantMap() : (*it)(a, b);
}

namespace bridge {

namespace {
std::vector<std::function<void()>> s_uiShutdown; // Slint thread only
} // namespace

void atUiShutdown(std::function<void()> f)
{
    s_uiShutdown.push_back(std::move(f));
}

void runUiShutdown()
{
    auto all = std::move(s_uiShutdown);
    s_uiShutdown.clear();
    for (auto &f : all)
        f();
}

} // namespace bridge

void bindCore(AppController &app, const Window &window, const Tray &tray, ImageCache *images, VideoRenderer *video,
              SlintHost *host)
{
    bindPlayer(app, window, images);
    registerQueueItems(app.playback()->queue());
    bindAccount(app.account(), window, images);
    bindApp(app, window);
    bindTheme(app.theme(), window);
    bindTray(app.player(), app.playback(), tray);
    bindVideo(app.player(), window, video);
    connectCallbacks(&app, window, tray, host);
    bindBrowse(app, window, images);
    bindSearch(app, window, images);
    bindNowPlaying(app, window, images, video);
    bindDialogs(app, window, images, host);
}
