// Area A: the pages (qml/BrowseView.qml and its rows) and the page stack of
// qml/Main.qml. Pages (core thread) holds the BrowsePages; each page's rows
// are mirrored into a Slint model of their own (Slint thread), and the open
// page's model is Browse.rows.

#include "Areas.h"
#include "ImageCache.h"
#include "ModelMirror.h"
#include "Pages.h"
#include "app/AppController.h"
#include "download/Downloader.h"
#include "player/Playback.h"
#include "ytm/Account.h"
#include "ytm/BrowsePage.h"

#include <QColor>

#include <cstdint>
#include <unordered_map>

using namespace bridge;

namespace {

// cover sizes in logical pixels (ui/page-rows.slint, ui/card.slint)
constexpr int kRowArt = 44;
constexpr int kCardArt = 172;
constexpr int kWideCardArt = 300;
constexpr int kHeaderArt = 232;
constexpr int kTopResultArt = 132;

struct ItemData
{
    std::string title;
    std::string subtitle;
    std::string artUrl;
    std::string duration;
    std::string kind;
    std::string videoId;
    std::string number;
    uint32_t color = 0; // ARGB, 0 for none
    bool explicitContent = false;
    bool unavailable = false;
    bool wide = false;
    bool playable = false;
};

struct ChipData
{
    std::string title;
    std::string label;
    bool iconOnly = false;
    bool selected = false;
};

struct ArtistData
{
    std::string name;
    bool linked = false;
};

// a row of a BrowsePage as plain data, for the trip to the Slint thread
struct RowData
{
    std::string type;
    std::string style;
    std::string title;
    std::string subtitle;
    std::string strapline;
    std::string description;
    std::string text;
    std::string artUrl;
    ItemData item;
    std::vector<ArtistData> artists;
    std::vector<ItemData> cards;
    std::vector<ChipData> chips;
    std::string sortTitle;
    std::vector<std::string> sortOptions;
    int sortSelected = -1;
    bool anyWide = false;
    bool compact = false;
    bool buttons = false;
    bool more = false;
    bool albumTracks = false;
    bool canPlay = false;
    bool canShuffle = false;
    bool canRadio = false;
    bool canDownload = false;
    bool hasMenu = false;
};

using RowModel = slint::Model<ui::PageRow>;
using RowVector = slint::VectorModel<ui::PageRow>;

bool has(const QVariantMap &map, const char *key)
{
    return !map.value(QLatin1String(key)).toString().isEmpty();
}

ItemData itemOf(const QVariantMap &item)
{
    ItemData d{ text(item, "title"), text(item, "subtitle"), text(item, "thumbnail"), text(item, "duration"),
                text(item, "kind"), text(item, "videoId"), text(item, "index") };
    const QColor color = QColor::fromString(item.value(QStringLiteral("color")).toString());
    if (color.isValid())
        d.color = color.rgba();
    d.explicitContent = item.value(QStringLiteral("explicit")).toBool();
    d.unavailable = item.value(QStringLiteral("unavailable")).toBool();
    d.wide = item.value(QStringLiteral("wide")).toBool();
    d.playable = has(item, "videoId") || has(item, "playPlaylistId") || d.kind == "station";
    return d;
}

// the buttons of a header or the top search result (qml/PlayButtons.qml)
void buttonsOf(const QVariantMap &target, RowData &d)
{
    d.canPlay = has(target, "playPlaylistId") || has(target, "playVideoId") || has(target, "videoId");
    d.canShuffle = has(target, "shufflePlaylistId");
    d.canRadio = has(target, "radioPlaylistId");
}

RowData rowOf(const QString &type, const QVariantMap &row)
{
    RowData d;
    d.type = utf8(type);
    d.title = text(row, "title");
    if (type == u"header") {
        d.style = text(row, "style");
        d.description = text(row, "description");
        d.strapline = text(row, "strapline");
        d.artUrl = text(row, "thumbnail");
        QStringList subtitle;
        for (const char *key : { "subtitle", "secondSubtitle" }) {
            if (has(row, key))
                subtitle.append(row.value(QLatin1String(key)).toString());
        }
        d.subtitle = utf8(subtitle.join(QStringLiteral(" • ")));
        for (const QVariant &v : row.value(QStringLiteral("artists")).toList()) {
            const QVariantMap a = v.toMap();
            d.artists.push_back({ text(a, "name"), has(a, "id") });
        }
        buttonsOf(row, d);
        const QString kind = row.value(QStringLiteral("kind")).toString();
        d.hasMenu = has(row, "browseId");
        d.canDownload = d.hasMenu && (kind == u"album" || kind == u"playlist");
    } else if (type == u"chips") {
        for (const QVariant &v : row.value(QStringLiteral("chips")).toList()) {
            const QVariantMap c = v.toMap();
            d.chips.push_back({ text(c, "title"), text(c, "label"), !has(c, "title") && text(c, "icon") == "close",
                                c.value(QStringLiteral("selected")).toBool() });
        }
        const QVariantMap sort = row.value(QStringLiteral("sort")).toMap();
        d.sortTitle = text(sort, "title");
        for (const QVariant &v : sort.value(QStringLiteral("options")).toList()) {
            const QVariantMap o = v.toMap();
            if (o.value(QStringLiteral("selected")).toBool())
                d.sortSelected = int(d.sortOptions.size());
            d.sortOptions.push_back(text(o, "title"));
        }
    } else if (type == u"item" || type == u"card") {
        const QVariantMap item = row.value(QStringLiteral("item")).toMap();
        d.item = itemOf(item);
        d.artUrl = d.item.artUrl;
        d.albumTracks = row.value(QStringLiteral("albumTracks")).toBool();
        buttonsOf(item, d);
    } else if (type == u"text") {
        d.text = text(row, "text");
    } else {
        // title, carousel, grid
        d.strapline = utf8(row.value(QStringLiteral("strapline")).toString().toUpper());
        d.compact = row.value(QStringLiteral("compact")).toBool();
        d.buttons = row.value(QStringLiteral("buttons")).toBool();
        d.more = !row.value(QStringLiteral("more")).toMap().isEmpty();
        // a title row's shelf shows its items as rows of their own
        if (type == u"title")
            return d;
        for (const QVariant &v : row.value(QStringLiteral("items")).toList()) {
            d.cards.push_back(itemOf(v.toMap()));
            d.anyWide = d.anyWide || d.cards.back().wide;
        }
    }
    return d;
}

ui::ListItem listItem(const ItemData &d)
{
    ui::ListItem item;
    item.title = str(d.title);
    item.subtitle = str(d.subtitle);
    item.art_url = str(d.artUrl);
    item.duration = str(d.duration);
    item.kind = str(d.kind);
    item.explicit_ = d.explicitContent;
    item.unavailable = d.unavailable;
    item.number = str(d.number);
    return item;
}

template<typename T, typename Data, typename Make>
std::shared_ptr<slint::VectorModel<T>> modelOf(const std::vector<Data> &list, Make make)
{
    auto model = std::make_shared<slint::VectorModel<T>>();
    for (const Data &d : list)
        model->push_back(make(d));
    return model;
}

// Slint thread
ui::PageRow makeRow(const RowData &d)
{
    ui::PageRow row;
    row.type = str(d.type);
    row.style = str(d.style);
    row.title = str(d.title);
    row.subtitle = str(d.subtitle);
    row.strapline = str(d.strapline);
    row.description = str(d.description);
    row.text = str(d.text);
    row.art_url = str(d.artUrl);
    row.item = listItem(d.item);
    row.video_id = str(d.item.videoId);
    row.playable = !d.item.videoId.empty();
    row.artists = modelOf<ui::ArtistLink>(d.artists, [](const ArtistData &a) {
        return ui::ArtistLink{ str(a.name), a.linked };
    });
    row.cards = modelOf<ui::PageCard>(d.cards, [](const ItemData &c) {
        ui::PageCard card;
        card.entry = listItem(c);
        card.video_id = str(c.videoId);
        card.wide = c.wide;
        card.playable = c.playable;
        card.color = slint::Color::from_argb_encoded(c.color);
        return card;
    });
    row.any_wide = d.anyWide;
    row.compact = d.compact;
    row.buttons = d.buttons;
    row.more = d.more;
    row.album_tracks = d.albumTracks;
    row.chips = modelOf<ui::PageChip>(d.chips, [](const ChipData &c) {
        return ui::PageChip{ str(c.title), str(c.label), c.iconOnly, c.selected };
    });
    row.sort_title = str(d.sortTitle);
    row.sort_options = modelOf<slint::SharedString>(d.sortOptions, [](const std::string &o) { return str(o); });
    row.sort_selected = d.sortSelected;
    row.can_play = d.canPlay;
    row.can_shuffle = d.canShuffle;
    row.can_radio = d.canRadio;
    row.can_download = d.canDownload;
    row.has_menu = d.hasMenu;
    return row;
}

// The rows of a page and one more at the end, the footer (loading more,
// errors), since a ListView holds nothing but its rows. Slint thread.
class WithFooter : public RowModel
{
public:
    explicit WithFooter(std::shared_ptr<RowModel> rows)
        : m_rows(std::move(rows))
        , m_listener(std::make_shared<Listener>(*this))
    {
        m_footer.type = "footer";
        m_rows->attach_peer(m_listener);
    }

    size_t row_count() const override { return m_rows->row_count() + 1; }

    std::optional<ui::PageRow> row_data(size_t i) const override
    {
        const size_t count = m_rows->row_count();
        if (i < count)
            return m_rows->row_data(i);
        if (i == count)
            return m_footer;
        return {};
    }

    void set_row_data(size_t i, const ui::PageRow &value) override
    {
        if (i < m_rows->row_count())
            m_rows->set_row_data(i, value);
    }

private:
    // the same interface Slint's own adapter models (MapModel, ...) listen through
    struct Listener : slint::private_api::ModelChangeListener
    {
        explicit Listener(WithFooter &model)
            : model(model)
        {
        }
        void row_added(size_t index, size_t count) override { model.notify_row_added(index, count); }
        void row_removed(size_t index, size_t count) override { model.notify_row_removed(index, count); }
        void row_changed(size_t index) override { model.notify_row_changed(index); }
        void reset() override { model.notify_reset(); }

        WithFooter &model;
    };

    std::shared_ptr<RowModel> m_rows;
    std::shared_ptr<Listener> m_listener;
    ui::PageRow m_footer;
};

// The pages' models on the Slint thread, by page id.
struct UiPages
{
    struct Page
    {
        std::shared_ptr<RowVector> rows;
        std::shared_ptr<WithFooter> view;
        // the row on top of the list when the page was left, and how far past it
        int topRow = 0;
        float topOffset = 0;
    };

    std::unordered_map<int, Page> pages;
    int current = -1;

    Page *find(int id)
    {
        const auto it = pages.find(id);
        return it == pages.end() ? nullptr : &it->second;
    }
};

UiPages &uiPages()
{
    static UiPages pages; // Slint thread only, emptied by atUiShutdown()
    return pages;
}

// The cover arrived: into the row at index, or wherever that row went since.
void putRowArt(const std::weak_ptr<RowModel> &weak, size_t index, const slint::SharedString &url,
               const slint::Image &image)
{
    auto m = weak.lock();
    if (!m)
        return;
    auto fits = [&](size_t i) {
        const auto row = m->row_data(i);
        return row && row->art_url == url;
    };
    if (!fits(index)) {
        index = m->row_count();
        for (size_t i = 0; i < m->row_count(); i++) {
            if (fits(i)) {
                index = i;
                break;
            }
        }
        if (index == m->row_count())
            return;
    }
    ui::PageRow row = *m->row_data(index);
    if (row.type == "header")
        row.art = image;
    else
        row.item.art = image;
    m->set_row_data(index, row);
}

void loadRowArt(ImageCache *images, const std::shared_ptr<RowModel> &model, size_t index, int pixels)
{
    const auto row = model->row_data(index);
    if (!row || row->art_url.empty())
        return;
    images->get(std::string(row->art_url), pixels,
                [weak = std::weak_ptr(model), index, url = row->art_url](const slint::Image &image) {
        putRowArt(weak, index, url, image);
    });
}

// the covers of a carousel or grid
void loadCardArt(ImageCache *images, const ui::PageRow &row, float scale)
{
    if (row.buttons)
        return;
    const int pixels = int((row.compact ? kRowArt : row.any_wide ? kWideCardArt : kCardArt) * scale);
    std::weak_ptr<slint::Model<ui::PageCard>> weak = row.cards;
    for (size_t i = 0; i < row.cards->row_count(); i++) {
        const ui::PageCard card = *row.cards->row_data(i);
        images->get(std::string(card.entry.art_url), pixels, [weak, i, url = card.entry.art_url](const slint::Image &image) {
            auto m = weak.lock();
            if (!m || i >= m->row_count())
                return;
            ui::PageCard c = *m->row_data(i);
            if (c.entry.art_url != url)
                return;
            c.entry.art = image;
            m->set_row_data(i, c);
        });
    }
}

// a new row: its covers, except a list row's, which waits until it shows
// (Browse.load-art), since a playlist may have thousands
void rowMade(const Window &window, ImageCache *images, const std::shared_ptr<RowVector> &model, size_t index,
             const RowData &d)
{
    auto ui = window.lock();
    if (!ui)
        return;
    const float scale = (*ui)->window().scale_factor();
    if (d.type == "header") {
        // the artist's banner spans the page
        const int pixels = d.style == "artist" ? int((*ui)->window().size().width) : int(kHeaderArt * scale);
        loadRowArt(images, model, index, pixels);
    } else if (d.type == "card") {
        loadRowArt(images, model, index, int(kTopResultArt * scale));
    } else if (d.type == "carousel" || d.type == "grid") {
        loadCardArt(images, *model->row_data(index), scale);
    }
}

// Slint thread: the open page and the state around it
struct Shown
{
    int id = -1;
    ui::PageMove move = ui::PageMove::Section;
    std::string section;
    bool canBack = false;
    bool canForward = false;
};

ui::PageMove moveOf(Pages::Move move)
{
    switch (move) {
    case Pages::Move::Push:
        return ui::PageMove::Push;
    case Pages::Move::Back:
        return ui::PageMove::Back;
    case Pages::Move::Section:
        break;
    }
    return ui::PageMove::Section;
}

void show(ui::MainWindow &w, const Shown &s)
{
    UiPages &pages = uiPages();
    auto &browse = w.global<ui::Browse>();
    if (UiPages::Page *left = pages.find(pages.current)) {
        left->topRow = browse.get_list_row();
        left->topOffset = browse.get_list_offset();
    }
    pages.current = s.id;
    UiPages::Page *page = pages.find(s.id);
    browse.set_rows(page ? std::shared_ptr<RowModel>(page->view) : std::make_shared<RowVector>());
    browse.set_restore_row(page ? page->topRow : 0);
    browse.set_restore_offset(page ? page->topOffset : 0);
    // before page-id: the view reads it when the page changes
    browse.set_page_move(s.move);
    browse.set_page_id(s.id);
    browse.set_can_back(s.canBack);
    browse.set_can_forward(s.canForward);
    w.global<ui::App>().set_section(str(s.section));
}

struct PageState
{
    bool loading = false;
    std::string error;
    bool canLoadMore = false;
};

PageState stateOf(const BrowsePage *page)
{
    return page ? PageState{ page->loading(), utf8(page->error()), page->canLoadMore() } : PageState{};
}

void showState(const Window &window, const BrowsePage *page)
{
    toUi(window, [s = stateOf(page)](ui::MainWindow &w) {
        auto &browse = w.global<ui::Browse>();
        browse.set_loading(s.loading);
        browse.set_error(str(s.error));
        browse.set_can_load_more(s.canLoadMore);
    });
}

// a new page: its rows into a model of their own, its state while it is open
void mirrorPage(Pages *pages, BrowsePage *page, int id, const Window &window, ImageCache *images)
{
    ModelMirror<RowData, ui::PageRow>::start(
        window, page,
        [](const QModelIndex &index) {
            return rowOf(index.data(BrowsePage::RowTypeRole).toString(), index.data(BrowsePage::RowRole).toMap());
        },
        makeRow,
        [id](ui::MainWindow &w, std::shared_ptr<RowVector> model) {
            UiPages &ui = uiPages();
            UiPages::Page &p = ui.pages[id];
            p.rows = model;
            p.view = std::make_shared<WithFooter>(model);
            if (ui.current == id)
                w.global<ui::Browse>().set_rows(p.view);
        },
        [window, images](const std::shared_ptr<RowVector> &model, size_t index, const RowData &d) {
            rowMade(window, images, model, index, d);
        });

    auto state = [pages, page, window] {
        if (pages->current() == page)
            showState(window, page);
    };
    QObject::connect(page, &BrowsePage::loadingChanged, page, state);
    QObject::connect(page, &BrowsePage::errorChanged, page, state);
}

// the Slint callbacks: they run on the Slint thread and post to the core
void connectCallbacks(AppController *app, Pages *pages, const Window &window, ImageCache *images)
{
    Playback *playback = app->playback();
    toUi(window, [=](ui::MainWindow &w) {
        auto &b = w.global<ui::Browse>();
        // back first closes Now Playing, as in the QML frontend
        b.on_back([window, pages] {
            if (auto ui = window.lock(); ui && (*ui)->global<ui::App>().get_now_playing_open()) {
                (*ui)->global<ui::App>().set_now_playing_open(false);
                return;
            }
            toCore(pages, [pages] { pages->back(); });
        });
        b.on_forward([pages] { toCore(pages, [pages] { pages->forward(); }); });
        b.on_search([window, pages](const slint::SharedString &query) {
            if (auto ui = window.lock())
                (*ui)->global<ui::App>().set_now_playing_open(false);
            toCore(pages, [pages, q = fromSlint(query)] { pages->openPage({ {}, {}, q }); });
        });

        b.on_activate([app, pages](int row, int card) {
            toCore(pages, [app, pages, row, card] { app->open(pages->item(row, card)); });
        });
        b.on_play([playback, pages](int row, int card) {
            toCore(pages, [playback, pages, row, card] { playback->play(pages->item(row, card)); });
        });
        b.on_shuffle([playback, pages](int row, int card) {
            toCore(pages, [playback, pages, row, card] {
                playback->playPlaylist(pages->item(row, card).value(QStringLiteral("shufflePlaylistId")).toString(),
                                       {}, true);
            });
        });
        b.on_radio([playback, pages](int row, int card) {
            toCore(pages, [playback, pages, row, card] {
                playback->playPlaylist(pages->item(row, card).value(QStringLiteral("radioPlaylistId")).toString(),
                                       {}, false);
            });
        });
        b.on_download([app, pages](int row) {
            toCore(pages, [app, pages, row] { app->downloads()->download(pages->item(row, -1)); });
        });
        b.on_copy_link([app, pages](int row) {
            toCore(pages, [app, pages, row] { app->copyLink(pages->item(row, -1)); });
        });
        b.on_rate_all([app, pages](const slint::SharedString &status) {
            toCore(pages, [app, pages, s = fromSlint(status)] {
                if (BrowsePage *page = pages->current())
                    app->account()->rateAll(page->trackIds(), s);
            });
        });
        b.on_chip([pages](int row, int chip) { toCore(pages, [pages, row, chip] { pages->applyChip(row, chip); }); });
        b.on_sort([pages](int row, int option) {
            toCore(pages, [pages, row, option] { pages->sortBy(row, option); });
        });
        b.on_more([app, pages](int row) {
            toCore(pages, [app, pages, row] {
                const QVariantMap more = pages->row(row).value(QStringLiteral("more")).toMap();
                app->openBrowse(more.value(QStringLiteral("browseId")).toString(),
                                more.value(QStringLiteral("params")).toString());
            });
        });
        b.on_artist([app, pages](int row, int index) {
            toCore(pages, [app, pages, row, index] {
                const QVariantList artists = pages->row(row).value(QStringLiteral("artists")).toList();
                if (index >= 0 && index < artists.size())
                    app->openBrowse(artists.at(index).toMap().value(QStringLiteral("id")).toString());
            });
        });
        b.on_load_more([pages] {
            toCore(pages, [pages] {
                if (BrowsePage *page = pages->current())
                    page->loadMore();
            });
        });
        b.on_retry([pages] {
            toCore(pages, [pages] {
                if (BrowsePage *page = pages->current())
                    page->canLoadMore() ? page->loadMore() : page->reload();
            });
        });
        // asked for while the row is being made: the model changes after that
        b.on_load_art([window, images](int row) {
            slint::invoke_from_event_loop([window, images, row] {
                auto ui = window.lock();
                UiPages::Page *page = uiPages().find(uiPages().current);
                if (!ui || !page || row < 0)
                    return;
                loadRowArt(images, page->rows, size_t(row), int(kRowArt * (*ui)->window().scale_factor()));
            });
        });

        w.global<ui::App>().on_section_requested([window, pages](const slint::SharedString &name,
                                                                 const slint::SharedString &browseId) {
            if (auto ui = window.lock()) {
                (*ui)->global<ui::App>().set_now_playing_open(false);
                (*ui)->global<ui::App>().set_section(name);
            }
            toCore(pages, [pages, n = fromSlint(name), id = fromSlint(browseId)] { pages->goSection(n, id); });
        });
    });
}

} // namespace

void bindBrowse(AppController &app, const Window &window, ImageCache *images)
{
    auto *pages = new Pages(app, &app);
    pages->setHooks({
        [pages, window, images](BrowsePage *page, int id) { mirrorPage(pages, page, id, window, images); },
        [window](int id) { toUi(window, [id](ui::MainWindow &) { uiPages().pages.erase(id); }); },
        [pages, window] {
            toUi(window, [s = Shown{ pages->currentId(), moveOf(pages->lastMove()),
                                     utf8(pages->shownSection()), pages->canBack(), pages->canForward() }](ui::MainWindow &w) { show(w, s); });
            showState(window, pages->current());
        },
    });
    toUi(window, [](ui::MainWindow &) {
        atUiShutdown([] {
            uiPages().pages.clear();
            uiPages().current = -1;
        });
    });

    // a: the row of the open page, b: the card in it (-1 for the row's own item)
    registerItemSource(QStringLiteral("page"), [pages](int row, int card) { return pages->item(row, card); });
    connectCallbacks(&app, pages, window, images);

    QObject::connect(&app, &AppController::navigateRequested, pages, [pages, window](const QVariantMap &target) {
        toUi(window, [](ui::MainWindow &w) { w.global<ui::App>().set_now_playing_open(false); });
        pages->openPage({ target.value(QStringLiteral("browseId")).toString(),
                          target.value(QStringLiteral("params")).toString(), {} });
    });
    // pages and the home cache belong to the account they were loaded for
    QObject::connect(app.account(), &Account::signedInChanged, pages, [pages] { pages->goStart(); });
    pages->goStart();
}
