#include "Areas.h"
#include "ImageCache.h"
#include "app/AppController.h"
#include "download/Downloader.h"
#include "player/Playback.h"
#include "player/Queue.h"
#include "ytm/Account.h"
#include "ytm/SearchSuggestions.h"

using namespace bridge;

namespace {

// cover size of a suggested item in logical pixels
constexpr int kRowArt = 44;

struct SuggestedQuery
{
    std::string text;
    std::string query;
};

struct SuggestedItem
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

struct Suggestions
{
    std::vector<SuggestedQuery> queries;
    std::vector<SuggestedItem> items;
};

// what the item menu needs to know to show its entries (ui/item-menu.slint)
struct Facts
{
    int serial = 0;
    bool track = false;
    bool queueable = false;
    bool radio = false;
    bool download = false;
    bool link = false;
    bool album = false;
    bool artist = false;
    std::string artistName;
    bool removable = false;
};

QString field(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).toString();
}

// the first artist with a page of its own
QVariantMap firstArtist(const QVariantMap &item)
{
    for (const QVariant &v : item.value(QStringLiteral("artists")).toList()) {
        const QVariantMap artist = v.toMap();
        if (!field(artist, "id").isEmpty())
            return artist;
    }
    return {};
}

// the conditions of the item menu
Facts factsOf(const QVariantMap &item, int serial)
{
    Facts f;
    f.serial = serial;
    f.track = !field(item, "videoId").isEmpty();
    const QString browseId = field(item, "browseId");
    const QString kind = field(item, "kind");
    f.queueable = f.track || !field(item, "playPlaylistId").isEmpty();
    f.radio = f.track || !field(item, "radioPlaylistId").isEmpty();
    f.download = f.track || ((kind == u"album" || kind == u"playlist") && !browseId.isEmpty());
    f.link = f.track || !browseId.isEmpty();
    f.album = !field(item.value(QStringLiteral("album")).toMap(), "id").isEmpty();
    const QVariantMap artist = firstArtist(item);
    f.artist = !artist.isEmpty();
    f.artistName = text(artist, "name");
    f.removable = f.track && !field(item, "editPlaylistId").isEmpty();
    return f;
}

// the selected tracks of a page: the actions that take many at once
void actOnMany(AppController &app, const QString &action, const QString &source, const std::vector<int> &rows)
{
    QVariantList items;
    for (int row : rows) {
        const QVariantMap item = lookupItem(source, row, -1);
        if (!field(item, "videoId").isEmpty())
            items.append(item);
    }
    if (items.isEmpty())
        return;
    Playback *playback = app.playback();
    if (action == u"playNext") {
        // each goes right after the playing track: the last first keeps the order
        for (auto it = items.crbegin(); it != items.crend(); ++it)
            playback->playNext(it->toMap());
    } else if (action == u"addToQueue") {
        playback->addTracksToQueue(items);
    } else if (action == u"addToPlaylist") {
        app.requestDialog(QStringLiteral("addToPlaylist"), { { QStringLiteral("items"), items } });
    } else if (action == u"download") {
        for (const QVariant &item : items)
            app.downloads()->download(item.toMap());
    } else if (action == u"removeFromPlaylist") {
        QStringList videoIds;
        QStringList setVideoIds;
        for (const QVariant &v : items) {
            const QVariantMap item = v.toMap();
            if (field(item, "setVideoId").isEmpty())
                continue;
            videoIds.append(field(item, "videoId"));
            setVideoIds.append(field(item, "setVideoId"));
        }
        app.account()->removeFromPlaylist(field(items.first().toMap(), "editPlaylistId"), videoIds, setVideoIds);
    }
}

void act(AppController &app, const QString &action, const QString &source, int a, int b)
{
    const QVariantMap item = lookupItem(source, a, b);
    if (item.isEmpty())
        return;
    Playback *playback = app.playback();
    if (action == u"playNext")
        playback->playNext(item);
    else if (action == u"addToQueue")
        playback->addToQueue(item);
    else if (action == u"startRadio")
        playback->startRadio(item);
    else if (action == u"addToPlaylist")
        app.requestDialog(QStringLiteral("addToPlaylist"), { { QStringLiteral("item"), item } });
    else if (action == u"download")
        app.downloads()->download(item);
    else if (action == u"removeFromPlaylist")
        app.account()->removeFromPlaylist(field(item, "editPlaylistId"), {field(item, "videoId")}, {field(item, "setVideoId")});
    else if (action == u"remove") {
        if (source == u"queue" && a != playback->queue()->current())
            playback->queue()->remove(a);
    } else if (action == u"copyLink")
        app.copyLink(item);
    else if (action == u"album")
        app.openBrowse(field(item.value(QStringLiteral("album")).toMap(), "id"));
    else if (action == u"artist")
        app.openBrowse(field(firstArtist(item), "id"));
}

void bindItemMenu(AppController &app, const Window &window)
{
    toUi(window, [app = &app, window](ui::MainWindow &w) {
        auto &menu = w.global<ui::ItemMenu>();
        menu.on_look_up([app, window](int serial, const slint::SharedString &source, int a, int b) {
            toCore(app, [window, serial, source = fromSlint(source), a, b] {
                toUi(window, [f = factsOf(lookupItem(source, a, b), serial)](ui::MainWindow &w) {
                    ui::ItemFacts facts;
                    facts.serial = f.serial;
                    facts.track = f.track;
                    facts.queueable = f.queueable;
                    facts.radio = f.radio;
                    facts.download = f.download;
                    facts.link = f.link;
                    facts.album = f.album;
                    facts.artist = f.artist;
                    facts.artist_name = str(f.artistName);
                    facts.removable = f.removable;
                    w.global<ui::ItemMenu>().set_facts(facts);
                });
            });
        });
        menu.on_act([app, window](const slint::SharedString &action, const slint::SharedString &source, int a, int b) {
            // a selected track of a page: the menu is for the whole selection,
            // which is done with afterwards
            std::vector<int> rows;
            if (source == "page" && b < 0) {
                if (auto ui = window.lock()) {
                    rows = selectedPageRows(**ui, a);
                    if (!rows.empty())
                        selectPageRows(**ui, -1, "clear");
                }
            }
            toCore(app, [app, action = fromSlint(action), source = fromSlint(source), a, b, rows] {
                if (rows.empty())
                    act(*app, action, source, a, b);
                else
                    actOnMany(*app, action, source, rows);
            });
        });
    });
}

ui::ListItem listItem(const SuggestedItem &d)
{
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
}

// suggestions of the search field: one SearchSuggestions on the core thread
void bindSuggestions(AppController &app, const Window &window, ImageCache *images)
{
    auto *suggestions = new SearchSuggestions(&app);
    Playback *playback = app.playback();

    // a: the row of a suggested item
    registerItemSource(QStringLiteral("suggestions"), [suggestions](int row, int) {
        return suggestions->items().value(row).toMap();
    });

    bind(window, suggestions, &SearchSuggestions::resultsChanged,
         [playback](SearchSuggestions *s) {
        Suggestions out;
        for (const QVariant &v : s->queries()) {
            const QVariantMap q = v.toMap();
            out.queries.push_back({ text(q, "text"), text(q, "query") });
        }
        const QString playing = playback->videoId();
        for (const QVariant &v : s->items()) {
            const QVariantMap item = v.toMap();
            const QString videoId = field(item, "videoId");
            out.items.push_back({ text(item, "title"), text(item, "subtitle"), text(item, "thumbnail"),
                                  text(item, "duration"), text(item, "kind"),
                                  item.value(QStringLiteral("explicit")).toBool(),
                                  item.value(QStringLiteral("unavailable")).toBool(),
                                  !videoId.isEmpty() && videoId == playing });
        }
        return out;
    },
         [images](ui::MainWindow &w, const Suggestions &s) {
        auto &search = w.global<ui::Search>();
        auto queries = std::make_shared<slint::VectorModel<ui::SearchQuery>>();
        for (const SuggestedQuery &q : s.queries) {
            ui::SearchQuery query;
            query.text = str(q.text);
            query.query = str(q.query);
            queries->push_back(query);
        }
        search.set_queries(queries);

        auto items = std::make_shared<slint::VectorModel<ui::ListItem>>();
        for (const SuggestedItem &d : s.items)
            items->push_back(listItem(d));
        search.set_items(items);
        const int pixels = int(kRowArt * w.window().scale_factor());
        for (size_t i = 0; i < s.items.size(); i++) {
            images->get(s.items[i].artUrl, pixels, [weak = std::weak_ptr(items), i](const slint::Image &image) {
                // newer suggestions may have replaced the model meanwhile
                auto m = weak.lock();
                if (!m || i >= m->row_count())
                    return;
                ui::ListItem row = *m->row_data(i);
                row.art = image;
                m->set_row_data(i, row);
            });
        }
    });

    toUi(window, [app = &app, suggestions](ui::MainWindow &w) {
        auto &search = w.global<ui::Search>();
        search.on_input([suggestions](const slint::SharedString &input) {
            toCore(suggestions, [suggestions, input = fromSlint(input)] { suggestions->setInput(input); });
        });
        search.on_open_item([app, suggestions](int row) {
            toCore(app, [app, suggestions, row] { app->open(suggestions->items().value(row).toMap()); });
        });
        search.on_trimmed([](const slint::SharedString &input) { return str(utf8(fromSlint(input).trimmed())); });
    });
}

} // namespace

void bindSearch(AppController &app, const Window &window, ImageCache *images)
{
    bindSuggestions(app, window, images);
    bindItemMenu(app, window);
}
