#include "Bindings.h"
#include "ImageCache.h"
#include "ModelMirror.h"
#include "SlintHost.h"
#include "VideoRenderer.h"
#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "integrations/Notifications.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/Queue.h"

#include <QColor>
#include <QCoreApplication>

using namespace bridge;

namespace {

// cover sizes in device pixels: the big one (the player bar shows it too), queue rows
constexpr int kBigArt = 640;
constexpr int kRowArt = 80;

struct QueueData
{
    std::string title;
    std::string subtitle;
    std::string artUrl;
    bool current = false;
};

struct TrackData
{
    std::string title;
    std::string subtitle;
    std::string artUrl;
};

TrackData trackOf(const QVariantMap &track)
{
    return { utf8(track.value(QStringLiteral("title")).toString()), utf8(Notifications::artistsOf(track)),
             utf8(track.value(QStringLiteral("thumbnail")).toString()) };
}

slint::Color color(const QColor &c)
{
    return slint::Color::from_argb_uint8(uint8_t(c.alpha()), uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()));
}

void bindPlayer(MpvPlayer *player, Playback *playback, Settings *settings, const Window &window, ImageCache *images)
{
    bind(window, player, &MpvPlayer::pausedChanged, [](MpvPlayer *p) { return p->paused(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_paused(v); });
    bind(window, player, &MpvPlayer::idleChanged, [](MpvPlayer *p) { return p->idle(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_idle(v); });
    bind(window, player, &MpvPlayer::positionChanged, [](MpvPlayer *p) { return float(p->position()); },
         [](MainWindow &w, float v) { w.global<Player>().set_position(v); });
    bind(window, player, &MpvPlayer::durationChanged, [](MpvPlayer *p) { return float(p->duration()); },
         [](MainWindow &w, float v) { w.global<Player>().set_duration(v); });
    bind(window, settings, &Settings::changed, [](Settings *s) { return float(s->volume()); },
         [](MainWindow &w, float v) { w.global<Player>().set_volume(v); });

    bind(window, playback, &Playback::loadingChanged, [](Playback *p) { return p->loading(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_loading(v); });
    bind(window, playback, &Playback::errorChanged, [](Playback *p) { return utf8(p->error()); },
         [](MainWindow &w, const std::string &v) { w.global<Player>().set_error(str(v)); });
    bind(window, playback, &Playback::queueStateChanged, [](Playback *p) { return p->hasNext(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_has_next(v); });
    bind(window, playback, &Playback::queueStateChanged, [](Playback *p) { return p->hasPrevious(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_has_previous(v); });
    bind(window, playback, &Playback::streamInfoChanged, [](Playback *p) { return p->videoPlaying(); },
         [](MainWindow &w, bool v) { w.global<Player>().set_video_playing(v); });
    bind(window, playback, &Playback::trackChanged, [](Playback *p) { return trackOf(p->track()); },
         [images, window](MainWindow &w, const TrackData &t) {
        w.global<Player>().set_title(str(t.title));
        w.global<Player>().set_subtitle(str(t.subtitle));
        const float scale = w.window().scale_factor();
        images->get(t.artUrl, int(kBigArt * scale), [window](const slint::Image &image) {
            if (auto ui = window.lock())
                (*ui)->global<Player>().set_art(image);
        });
    });

    ModelMirror<QueueData, QueueRow>::start(
        window, playback->queue(),
        [](const QModelIndex &index) {
            const QVariantMap item = index.data(Queue::ItemRole).toMap();
            return QueueData{ utf8(item.value(QStringLiteral("title")).toString()), utf8(Notifications::artistsOf(item)),
                              utf8(item.value(QStringLiteral("thumbnail")).toString()),
                              index.data(Queue::CurrentRole).toBool() };
        },
        [](const QueueData &d) {
            QueueRow row;
            row.title = str(d.title);
            row.subtitle = str(d.subtitle);
            row.art_url = str(d.artUrl);
            row.current = d.current;
            return row;
        },
        [](MainWindow &w, std::shared_ptr<slint::VectorModel<QueueRow>> model) { w.global<Player>().set_queue(model); },
        [images](const std::shared_ptr<slint::VectorModel<QueueRow>> &model, size_t, const QueueData &d) {
            // rows move while the picture loads: it goes to every row with this cover
            images->get(d.artUrl, kRowArt, [weak = std::weak_ptr(model), url = str(d.artUrl)](const slint::Image &image) {
                auto m = weak.lock();
                if (!m)
                    return;
                for (size_t i = 0; i < m->row_count(); i++) {
                    QueueRow row = *m->row_data(i);
                    if (row.art_url == url) {
                        row.art = image;
                        m->set_row_data(i, row);
                    }
                }
            });
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
         [](MainWindow &w, const Colors &c) {
        auto &theme = w.global<Theme>();
        theme.set_accent(color(c.accent));
        theme.set_background(color(c.background));
        theme.set_surface(color(c.surface));
        theme.set_surface_high(color(c.surfaceHigh));
    });
}

void bindTray(MpvPlayer *player, Playback *playback, const Tray &tray)
{
    bind(tray, playback, &Playback::trackChanged,
         [](Playback *p) {
        const TrackData t = trackOf(p->track());
        return t.title.empty() ? std::string("Nothing playing") : t.title + " - " + t.subtitle;
    },
         [](TrayIcon &t, const std::string &line) { t.set_now_playing(str(line)); });
    bind(tray, player, &MpvPlayer::pausedChanged, [](MpvPlayer *p) { return !p->paused() && !p->idle(); },
         [](TrayIcon &t, bool playing) { t.set_playing(playing); });
}

// the Slint callbacks: they run on the Slint thread and post to the core
void connectCallbacks(AppController *app, const Window &window, const Tray &tray, SlintHost *host)
{
    MpvPlayer *player = app->player();
    Playback *playback = app->playback();
    toUi(window, [app, player, playback](MainWindow &w) {
        auto &p = w.global<Player>();
        p.on_toggle_pause([player] { toCore(player, [player] { player->togglePause(); }); });
        p.on_next([playback] { toCore(playback, [playback] { playback->next(); }); });
        p.on_previous([playback] { toCore(playback, [playback] { playback->previous(); }); });
        p.on_seek([player](float s) { toCore(player, [player, s] { player->seek(s); }); });
        p.on_set_volume([app](float v) { toCore(app, [app, v] { app->setVolumeSlider(v); }); });
        p.on_play_index([playback](int i) { toCore(playback, [playback, i] { playback->playIndex(i); }); });
        p.on_play_text([playback](const slint::SharedString &text) {
            toCore(playback, [playback, text = QString::fromUtf8(text.data(), qsizetype(text.size()))] {
                playback->playText(text);
            });
        });
    });
    toUi(tray, [player, playback, host](TrayIcon &t) {
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
        toUi(window, [video, mpv = player->renderHandle()](MainWindow &) { video->setSource(mpv); });
    };
    QObject::connect(player, &MpvPlayer::activeDeckChanged, player, source);
    source();
    toUi(window, [player, video](MainWindow &) {
        video->onOutput = [player](bool on) { toCore(player, [player, on] { player->setVideoOutput(on); }); };
    });
}

} // namespace

void bindCore(AppController &app, const Window &window, const Tray &tray, ImageCache *images, VideoRenderer *video,
              SlintHost *host)
{
    bindPlayer(app.player(), app.playback(), app.settings(), window, images);
    bindTheme(app.theme(), window);
    bindTray(app.player(), app.playback(), tray);
    bindVideo(app.player(), window, video);
    connectCallbacks(&app, window, tray, host);
}
