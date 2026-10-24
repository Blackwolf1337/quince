// Area C: the Now Playing view, the lyrics and the mini player.

#include "Areas.h"
#include "ImageCache.h"
#include "MiniPlayerWindow.h"
#include "VideoRenderer.h"
#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "lyrics/Lyrics.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"

#include <QColor>

#include <memory>
#include <string>
#include <vector>

using namespace bridge;

namespace {

struct LineData
{
    float time;
    std::string text;
    std::string romanized;
};

struct LyricsData
{
    std::vector<LineData> lines;
    bool synced;
    std::string source;
    std::string sourceUrl;
};

struct CaptionData
{
    std::string name;
    bool generated;
};

struct CaptionsData
{
    std::vector<CaptionData> captions;
    int current;
};

struct LookData
{
    float glowScale, ambientStrength, ambientSize;
    std::string visualizer;
    int videoHeight;
    std::string lyricsEffect;
    bool lyricsTimeCodes;
    std::string lyricsGapText;
};

std::string text(const QVariantMap &map, const char *key)
{
    return utf8(map.value(QLatin1String(key)).toString());
}

QString fromSlint(const slint::SharedString &s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

template<typename Row, typename Data, typename Make>
std::shared_ptr<slint::VectorModel<Row>> modelOf(const std::vector<Data> &list, Make make)
{
    auto model = std::make_shared<slint::VectorModel<Row>>();
    for (const Data &d : list)
        model->push_back(make(d));
    return model;
}

void bindTrack(Playback *playback, const Window &window, ImageCache *images)
{
    struct Track
    {
        std::string album, thumbnail;
    };
    // the blurred cover asked for last (Slint thread only)
    auto shown = std::make_shared<std::string>();
    bind(window, playback, &Playback::trackChanged,
         [](Playback *p) {
        const QVariantMap track = p->track();
        return Track{ text(track.value(QStringLiteral("album")).toMap(), "name"), text(track, "thumbnail") };
    },
         [images, window, shown](ui::MainWindow &w, const Track &t) {
        w.global<ui::Player>().set_album(str(t.album));
        if (*shown == t.thumbnail)
            return;
        *shown = t.thumbnail;
        images->getBlurred(t.thumbnail, [window, shown, url = t.thumbnail](const slint::Image &image) {
            auto ui = window.lock();
            if (ui && *shown == url)
                (*ui)->global<ui::Player>().set_art_blurred(image);
        });
    });
}

void bindLyrics(Lyrics *lyrics, const Window &window)
{
    bind(window, lyrics, &Lyrics::linesChanged,
         [](Lyrics *l) {
        LyricsData data{ {}, l->synced(), utf8(l->source()), utf8(l->sourceUrl()) };
        for (const QVariant &v : l->lines()) {
            const QVariantMap line = v.toMap();
            data.lines.push_back({ line.value(QStringLiteral("time")).toFloat(), text(line, "text"),
                                   text(line, "romanized") });
        }
        return data;
    },
         [](ui::MainWindow &w, const LyricsData &d) {
        auto &p = w.global<ui::Player>();
        p.set_lyrics_synced(d.synced);
        p.set_lyrics_source(str(d.source));
        p.set_lyrics_source_url(str(d.sourceUrl));
        p.set_lyrics(modelOf<ui::LyricsLine>(d.lines, [](const LineData &l) {
            return ui::LyricsLine{ l.time, str(l.text), str(l.romanized) };
        }));
    });
    bind(window, lyrics, &Lyrics::loadingChanged, [](Lyrics *l) { return l->loading(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Player>().set_lyrics_loading(v); });
    bind(window, lyrics, &Lyrics::currentLineChanged, [](Lyrics *l) { return l->currentLine(); },
         [](ui::MainWindow &w, int v) { w.global<ui::Player>().set_lyrics_line(v); });
}

void bindVideoState(MpvPlayer *player, Playback *playback, const Window &window)
{
    // the video's first frame is near after each restart
    auto restarts = std::make_shared<int>(0); // core thread only
    QObject::connect(player, &MpvPlayer::playbackRestarted, player, [window, restarts] {
        toUi(window, [n = ++*restarts](ui::MainWindow &w) { w.global<ui::Player>().set_restarts(n); });
    });

    bind(window, playback, &Playback::captionsChanged,
         [](Playback *p) {
        CaptionsData data{ {}, p->caption() };
        for (const QVariant &v : p->captions()) {
            const QVariantMap c = v.toMap();
            data.captions.push_back({ text(c, "name"), c.value(QStringLiteral("generated")).toBool() });
        }
        return data;
    },
         [](ui::MainWindow &w, const CaptionsData &d) {
        auto &p = w.global<ui::Player>();
        p.set_captions(modelOf<ui::Caption>(d.captions, [](const CaptionData &c) {
            return ui::Caption{ str(c.name), c.generated };
        }));
        p.set_caption(d.current);
    });
}

void bindLook(Settings *settings, ColorTheme *theme, const Window &window)
{
    bind(window, theme, &ColorTheme::colorsChanged, [](ColorTheme *t) { return t->glow(); },
         [](ui::MainWindow &w, const QColor &c) {
        w.global<ui::App>().set_glow(slint::Color::from_argb_uint8(uint8_t(c.alpha()), uint8_t(c.red()),
                                                                   uint8_t(c.green()), uint8_t(c.blue())));
    });
    bind(window, settings, &Settings::changed,
         [](Settings *s) {
        return LookData{ s->glowStrength() / 100.0f, s->ambientStrength() / 100.0f, s->ambientSize() / 100.0f,
                         utf8(s->visualizer()), s->videoHeight(), utf8(s->lyricsEffect()), s->lyricsTimeCodes(),
                         utf8(s->lyricsGapText()) };
    },
         [](ui::MainWindow &w, const LookData &d) {
        auto &a = w.global<ui::App>();
        a.set_glow_scale(d.glowScale);
        a.set_ambient_strength(d.ambientStrength);
        a.set_ambient_size(d.ambientSize);
        a.set_visualizer(str(d.visualizer));
        a.set_video_height(d.videoHeight);
        a.set_lyrics_effect(str(d.lyricsEffect));
        a.set_lyrics_time_codes(d.lyricsTimeCodes);
        a.set_lyrics_gap_text(str(d.lyricsGapText));
    });
}

void bindMiniPlayer(AppController &app, const Window &window, VideoRenderer *video)
{
    MpvPlayer *player = app.player();
    Settings *settings = app.settings();
    // made on the Slint thread, used only there
    auto slot = std::make_shared<std::weak_ptr<MiniPlayerWindow>>();

    toUi(window, [window, video, slot, settings](ui::MainWindow &w) {
        auto mini = std::make_shared<MiniPlayerWindow>(window, video);
        *slot = mini;
        mini->onWidthChosen = [settings](int width) {
            toCore(settings, [settings, width] { settings->setMiniWidth(width); });
        };
        w.on_mini_player_requested([slot] {
            if (auto m = slot->lock())
                m->open();
        });
        w.global<ui::App>().on_mini_state_changed([slot](const ui::MiniState &state) {
            if (auto m = slot->lock())
                m->setState(state);
        });
        // the window and its render context go before the core stops
        atUiShutdown([mini] { mini->destroy(); });
    });

    auto source = [window, player, slot] {
        toUi(window, [slot, mpv = player->renderHandle()](ui::MainWindow &) {
            if (auto m = slot->lock())
                m->setSource(mpv);
        });
    };
    QObject::connect(player, &MpvPlayer::activeDeckChanged, player, source);
    source();
    bind(window, settings, &Settings::changed, [](Settings *s) { return s->miniWidth(); },
         [slot](ui::MainWindow &, int width) {
        if (auto m = slot->lock())
            m->setWidth(width);
    });
}

// the Slint callbacks of the area: they run on the Slint thread and post to the core
void connectCallbacks(AppController &app, const Window &window)
{
    Playback *playback = app.playback();
    Settings *settings = app.settings();
    toUi(window, [playback, settings](ui::MainWindow &w) {
        auto &p = w.global<ui::Player>();
        p.on_set_caption([playback, settings](int index) {
            toCore(playback, [playback, settings, index] {
                playback->setCaption(index);
                // the next videos take this language by themselves
                const QVariantMap caption = playback->captions().value(index).toMap();
                settings->setCaptionLanguage(index >= 0 ? caption.value(QStringLiteral("language")).toString()
                                                        : QString());
            });
        });

        auto &a = w.global<ui::App>();
        a.on_set_visualizer([settings](const slint::SharedString &style) {
            toCore(settings, [settings, s = fromSlint(style)] { settings->setVisualizer(s); });
        });
        a.on_set_video_height([settings](int height) {
            toCore(settings, [settings, height] { settings->setVideoHeight(height); });
        });
    });
}

} // namespace

void bindNowPlaying(AppController &app, const Window &window, ImageCache *images, VideoRenderer *video)
{
    bindTrack(app.playback(), window, images);
    bindLyrics(app.lyrics(), window);
    bindVideoState(app.player(), app.playback(), window);
    bindLook(app.settings(), app.theme(), window);
    bindMiniPlayer(app, window, video);
    connectCallbacks(app, window);
}
