#include "ytm/Parser.h"
#include "ytm/Json.h"

#include <QRegularExpression>

namespace ytm {
namespace {

const QString kSeparator = QStringLiteral(" • ");

struct Endpoint
{
    QString browseId;
    QString params;
    QString pageType;
    QString videoId;
    QString playlistId;
    QString videoType;
    bool watchPlaylist = false;

    bool isBrowse() const { return !browseId.isEmpty(); }
    bool isWatch() const { return !videoId.isEmpty(); }
    bool isEmpty() const { return !isBrowse() && !isWatch() && playlistId.isEmpty(); }
};

Endpoint endpointOf(const QJsonValue &navigationEndpoint)
{
    Endpoint e;
    const QJsonObject endpoint = navigationEndpoint.toObject();
    if (const QJsonObject browse = endpoint.value(u"browseEndpoint").toObject(); !browse.isEmpty()) {
        e.browseId = browse.value(u"browseId").toString();
        e.params = browse.value(u"params").toString();
        e.pageType = at(browse, {"browseEndpointContextSupportedConfigs", "browseEndpointContextMusicConfig",
                                 "pageType"}).toString();
    } else if (const QJsonObject watch = endpoint.value(u"watchEndpoint").toObject(); !watch.isEmpty()) {
        e.videoId = watch.value(u"videoId").toString();
        e.playlistId = watch.value(u"playlistId").toString();
        e.params = watch.value(u"params").toString();
        e.videoType = at(watch, {"watchEndpointMusicSupportedConfigs", "watchEndpointMusicConfig",
                                 "musicVideoType"}).toString();
    } else if (const QJsonObject list = endpoint.value(u"watchPlaylistEndpoint").toObject(); !list.isEmpty()) {
        e.playlistId = list.value(u"playlistId").toString();
        e.params = list.value(u"params").toString();
        e.watchPlaylist = true;
    }
    return e;
}

QString kindForPageType(const QString &pageType)
{
    if (pageType == u"MUSIC_PAGE_TYPE_ALBUM" || pageType == u"MUSIC_PAGE_TYPE_AUDIOBOOK")
        return QStringLiteral("album");
    if (pageType == u"MUSIC_PAGE_TYPE_ARTIST" || pageType == u"MUSIC_PAGE_TYPE_USER_CHANNEL"
        || pageType == u"MUSIC_PAGE_TYPE_LIBRARY_ARTIST")
        return QStringLiteral("artist");
    if (pageType == u"MUSIC_PAGE_TYPE_PLAYLIST")
        return QStringLiteral("playlist");
    if (pageType == u"MUSIC_PAGE_TYPE_PODCAST_SHOW_DETAIL_PAGE")
        return QStringLiteral("podcast");
    if (pageType == u"MUSIC_PAGE_TYPE_NON_MUSIC_AUDIO_TRACK_PAGE")
        return QStringLiteral("episode");
    return QStringLiteral("page");
}

QString kindForVideoType(const QString &videoType)
{
    if (videoType == u"MUSIC_VIDEO_TYPE_PODCAST_EPISODE")
        return QStringLiteral("episode");
    if (videoType.isEmpty() || videoType == u"MUSIC_VIDEO_TYPE_ATV")
        return QStringLiteral("song");
    return QStringLiteral("video");
}

void applyBrowse(QVariantMap &item, const Endpoint &e)
{
    item.insert(QStringLiteral("kind"), kindForPageType(e.pageType));
    item.insert(QStringLiteral("browseId"), e.browseId);
    if (!e.params.isEmpty())
        item.insert(QStringLiteral("params"), e.params);
}

void applyWatch(QVariantMap &item, const Endpoint &e, const QString &videoType)
{
    item.insert(QStringLiteral("kind"), kindForVideoType(videoType));
    item.insert(QStringLiteral("videoId"), e.videoId);
    if (!e.playlistId.isEmpty())
        item.insert(QStringLiteral("playlistId"), e.playlistId);
    if (!videoType.isEmpty())
        item.insert(QStringLiteral("videoType"), videoType);
}

QString bestThumbnail(const QJsonValue &thumbnails)
{
    QString best;
    int bestWidth = -1;
    for (const QJsonValue &v : thumbnails.toArray()) {
        const QJsonObject t = v.toObject();
        const int width = t.value(u"width").toInt();
        if (width > bestWidth) {
            bestWidth = width;
            best = t.value(u"url").toString();
        }
    }
    if (best.startsWith(u"//"))
        best.prepend(u"https:");
    return best;
}

QString thumbnailOf(const QJsonObject &r)
{
    for (const QJsonValue &v : {
             at(r, {"thumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails"}),
             at(r, {"thumbnailRenderer", "musicThumbnailRenderer", "thumbnail", "thumbnails"}),
             at(r, {"thumbnail", "croppedSquareThumbnailRenderer", "thumbnail", "thumbnails"}),
             at(r, {"foregroundThumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails"}),
             at(r, {"thumbnail", "thumbnails"}),
         }) {
        if (v.isArray())
            return bestThumbnail(v);
    }
    return {};
}

bool isDuration(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+:)*\\d+:\\d\\d$"));
    return re.match(text).hasMatch();
}

int durationSeconds(const QString &text)
{
    int seconds = 0;
    for (const QString &part : text.split(u':'))
        seconds = seconds * 60 + part.toInt();
    return seconds;
}

bool isYear(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}$"));
    return re.match(text).hasMatch();
}

void setDuration(QVariantMap &item, const QString &text)
{
    item.insert(QStringLiteral("duration"), text);
    item.insert(QStringLiteral("durationSeconds"), durationSeconds(text));
}

// Artists, album, year and duration from subtitle runs like
// "Titel • Daft Punk und Julian Casablancas • Discovery • 3:57". The parts
// with links are recognised by their page type; a leading unlinked part is
// the localized type label when typeLabel is set.
void applyInfoRuns(QVariantMap &item, const QJsonArray &runs, bool typeLabel)
{
    QList<QJsonArray> segments(1);
    for (const QJsonValue &run : runs) {
        if (run.toObject().value(u"text").toString() == kSeparator)
            segments.append(QJsonArray());
        else
            segments.last().append(run);
    }
    segments.removeIf([](const QJsonArray &s) { return s.isEmpty(); });

    qsizetype first = 0;
    if (typeLabel && segments.size() >= 2 && segments[0].size() == 1
        && !segments[0][0].toObject().contains(u"navigationEndpoint")) {
        item.insert(QStringLiteral("typeLabel"), segments[0][0].toObject().value(u"text").toString());
        first = 1;
    }

    QVariantList artists;
    QVariantMap album;
    QString artistText;
    for (qsizetype i = first; i < segments.size(); ++i) {
        QString text;
        bool hasArtist = false;
        for (const QJsonValue &v : std::as_const(segments[i])) {
            const QJsonObject run = v.toObject();
            const QString runText = run.value(u"text").toString();
            text += runText;
            const Endpoint e = endpointOf(run.value(u"navigationEndpoint"));
            if (!e.isBrowse())
                continue;
            const QString kind = kindForPageType(e.pageType);
            if (kind == u"artist") {
                artists.append(QVariantMap{{QStringLiteral("name"), runText}, {QStringLiteral("id"), e.browseId}});
                hasArtist = true;
            } else if (kind == u"album") {
                album = {{QStringLiteral("name"), runText}, {QStringLiteral("id"), e.browseId}};
            }
        }
        if (isDuration(text))
            setDuration(item, text);
        else if (isYear(text))
            item.insert(QStringLiteral("year"), text);
        else if (hasArtist && artistText.isEmpty())
            artistText = text;
    }

    if (artistText.isEmpty() && first < segments.size()) {
        // an artist without a link, e.g. "Asma Teji • 3,1 Mio. Wiedergaben"
        const QJsonArray &segment = segments[first];
        QString text;
        bool linked = false;
        for (const QJsonValue &v : segment) {
            text += v.toObject().value(u"text").toString();
            linked = linked || v.toObject().contains(u"navigationEndpoint");
        }
        if (!linked && !isDuration(text) && !isYear(text))
            artistText = text;
    }

    if (!artists.isEmpty())
        item.insert(QStringLiteral("artists"), artists);
    if (!album.isEmpty())
        item.insert(QStringLiteral("album"), album);
    if (!artistText.isEmpty())
        item.insert(QStringLiteral("artistText"), artistText);
}

QString runsText(const QJsonArray &runs)
{
    QString out;
    for (const QJsonValue &run : runs)
        out += run.toObject().value(u"text").toString();
    return out;
}

// Radio and shuffle playlists, like status and ownership from an item's
// context menu.
void applyMenu(QVariantMap &item, const QJsonValue &menu)
{
    const QString like = at(menu, {"menuRenderer", "topLevelButtons", 0, "likeButtonRenderer", "likeStatus"}).toString();
    if (!like.isEmpty())
        item.insert(QStringLiteral("likeStatus"), like);

    for (const QJsonValue &v : at(menu, {"menuRenderer", "items"}).toArray()) {
        const QJsonObject o = v.toObject();

        // a toggle whose default action is "like" means the item is not liked yet
        const QString toggle = at(o, {"toggleMenuServiceItemRenderer", "defaultServiceEndpoint", "likeEndpoint",
                                      "status"}).toString();
        if (!toggle.isEmpty() && !item.contains(QStringLiteral("likeStatus")))
            item.insert(QStringLiteral("likeStatus"),
                        toggle == u"LIKE" ? QStringLiteral("INDIFFERENT") : QStringLiteral("LIKE"));

        const QJsonObject entry = o.value(u"menuNavigationItemRenderer").toObject();
        if (entry.isEmpty())
            continue;
        if (at(entry, {"navigationEndpoint", "playlistEditorEndpoint"}).isObject())
            item.insert(QStringLiteral("owned"), true);
        const QString icon = at(entry, {"icon", "iconType"}).toString();
        const Endpoint e = endpointOf(entry.value(u"navigationEndpoint"));
        if (e.playlistId.isEmpty())
            continue;
        if (icon == u"MIX")
            item.insert(QStringLiteral("radioPlaylistId"), e.playlistId);
        else if (icon == u"MUSIC_SHUFFLE")
            item.insert(QStringLiteral("shufflePlaylistId"), e.playlistId);
    }
}

bool hasExplicitBadge(const QJsonValue &badges)
{
    for (const QJsonValue &b : badges.toArray()) {
        if (at(b, {"musicInlineBadgeRenderer", "icon", "iconType"}).toString() == u"MUSIC_EXPLICIT_BADGE")
            return true;
    }
    return false;
}

void applyPlayButton(QVariantMap &item, const Endpoint &play)
{
    if (!play.playlistId.isEmpty())
        item.insert(QStringLiteral("playPlaylistId"), play.playlistId);
    if (play.isWatch() && !item.contains(QStringLiteral("videoId")))
        item.insert(QStringLiteral("playVideoId"), play.videoId);
}

// musicResponsiveListItemRenderer: a row in a list
QVariantMap parseListItem(const QJsonObject &r, bool typeLabel)
{
    QVariantMap item;
    const QJsonArray flex = r.value(u"flexColumns").toArray();
    auto column = [&flex](int i) {
        return at(flex, {i, "musicResponsiveListItemFlexColumnRenderer", "text"});
    };

    item.insert(QStringLiteral("title"), textOf(column(0)));

    const Endpoint own = endpointOf(r.value(u"navigationEndpoint"));
    const Endpoint title = endpointOf(at(column(0), {"runs", 0, "navigationEndpoint"}));
    const Endpoint play = endpointOf(at(r, {"overlay", "musicItemThumbnailOverlayRenderer", "content",
                                            "musicPlayButtonRenderer", "playNavigationEndpoint"}));
    QString videoId = at(r, {"playlistItemData", "videoId"}).toString();

    if (own.isBrowse()) {
        applyBrowse(item, own);
    } else if (title.isBrowse()) {
        applyBrowse(item, title);
    } else {
        const Endpoint &watch = title.isWatch() ? title : play.isWatch() ? play : own;
        if (videoId.isEmpty())
            videoId = watch.videoId;
        if (!videoId.isEmpty()) {
            Endpoint e = watch;
            e.videoId = videoId;
            applyWatch(item, e, watch.videoType.isEmpty() ? play.videoType : watch.videoType);
        } else if (play.watchPlaylist) {
            item.insert(QStringLiteral("kind"), QStringLiteral("station"));
            item.insert(QStringLiteral("playlistId"), play.playlistId);
        }
    }
    if (own.isBrowse() || title.isBrowse())
        applyPlayButton(item, play);

    QJsonArray infoRuns;
    for (int i = 1; i < flex.size(); ++i) {
        const QJsonArray runs = column(i).toObject().value(u"runs").toArray();
        if (runs.isEmpty())
            continue;
        if (!infoRuns.isEmpty())
            infoRuns.append(QJsonObject{{QStringLiteral("text"), kSeparator}});
        for (const QJsonValue &run : runs)
            infoRuns.append(run);
    }
    item.insert(QStringLiteral("subtitle"), runsText(infoRuns));
    applyInfoRuns(item, infoRuns, typeLabel);

    const QString fixed = textOf(at(r, {"fixedColumns", 0, "musicResponsiveListItemFixedColumnRenderer", "text"}));
    if (isDuration(fixed))
        setDuration(item, fixed);

    const QString index = textOf(r.value(u"index"));
    if (!index.isEmpty())
        item.insert(QStringLiteral("index"), index);

    const QString thumbnail = thumbnailOf(r);
    if (!thumbnail.isEmpty())
        item.insert(QStringLiteral("thumbnail"), thumbnail);
    if (hasExplicitBadge(r.value(u"badges")))
        item.insert(QStringLiteral("explicit"), true);

    const QString setVideoId = at(r, {"playlistItemData", "playlistSetVideoId"}).toString();
    if (!setVideoId.isEmpty())
        item.insert(QStringLiteral("setVideoId"), setVideoId);
    if (r.value(u"musicItemRendererDisplayPolicy").toString() == u"MUSIC_ITEM_RENDERER_DISPLAY_POLICY_GREY_OUT")
        item.insert(QStringLiteral("unavailable"), true);

    applyMenu(item, r.value(u"menu"));
    return item;
}

// musicTwoRowItemRenderer: a card in a carousel or grid
QVariantMap parseTwoRowItem(const QJsonObject &r)
{
    QVariantMap item;
    item.insert(QStringLiteral("title"), textOf(r.value(u"title")));

    Endpoint own = endpointOf(r.value(u"navigationEndpoint"));
    if (own.isEmpty())
        own = endpointOf(at(r, {"title", "runs", 0, "navigationEndpoint"}));
    const Endpoint play = endpointOf(at(r, {"thumbnailOverlay", "musicItemThumbnailOverlayRenderer", "content",
                                            "musicPlayButtonRenderer", "playNavigationEndpoint"}));

    if (own.isBrowse()) {
        applyBrowse(item, own);
        applyPlayButton(item, play);
    } else if (own.isWatch()) {
        applyWatch(item, own, own.videoType);
    } else if (own.watchPlaylist) {
        item.insert(QStringLiteral("kind"), QStringLiteral("station"));
        item.insert(QStringLiteral("playlistId"), own.playlistId);
    }

    const QJsonArray runs = at(r, {"subtitle", "runs"}).toArray();
    item.insert(QStringLiteral("subtitle"), runsText(runs));
    applyInfoRuns(item, runs, true);

    const QString thumbnail = thumbnailOf(r);
    if (!thumbnail.isEmpty())
        item.insert(QStringLiteral("thumbnail"), thumbnail);
    if (r.value(u"aspectRatio").toString().contains(u"16_9"))
        item.insert(QStringLiteral("wide"), true);
    if (hasExplicitBadge(r.value(u"subtitleBadges")))
        item.insert(QStringLiteral("explicit"), true);

    applyMenu(item, r.value(u"menu"));
    return item;
}

// musicMultiRowListItemRenderer: podcast episodes
QVariantMap parseMultiRowItem(const QJsonObject &r)
{
    QVariantMap item;
    item.insert(QStringLiteral("title"), textOf(r.value(u"title")));
    item.insert(QStringLiteral("subtitle"), textOf(r.value(u"subtitle")));
    item.insert(QStringLiteral("description"), textOf(r.value(u"description")));

    const Endpoint play = endpointOf(r.value(u"onTap"));
    const Endpoint button = endpointOf(at(r, {"overlay", "musicItemThumbnailOverlayRenderer", "content",
                                              "musicPlayButtonRenderer", "playNavigationEndpoint"}));
    const Endpoint &watch = button.isWatch() ? button : play;
    if (watch.isWatch())
        applyWatch(item, watch, QStringLiteral("MUSIC_VIDEO_TYPE_PODCAST_EPISODE"));
    else if (play.isBrowse())
        applyBrowse(item, play);

    const QString thumbnail = thumbnailOf(r);
    if (!thumbnail.isEmpty())
        item.insert(QStringLiteral("thumbnail"), thumbnail);
    return item;
}

// musicCardShelfRenderer: the top result of a search
QVariantMap parseCardItem(const QJsonObject &r)
{
    QVariantMap item;
    item.insert(QStringLiteral("title"), textOf(r.value(u"title")));

    Endpoint own = endpointOf(at(r, {"title", "runs", 0, "navigationEndpoint"}));
    if (own.isEmpty())
        own = endpointOf(r.value(u"onTap"));
    if (own.isBrowse())
        applyBrowse(item, own);
    else if (own.isWatch())
        applyWatch(item, own, own.videoType);

    const QJsonArray runs = at(r, {"subtitle", "runs"}).toArray();
    item.insert(QStringLiteral("subtitle"), runsText(runs));
    applyInfoRuns(item, runs, true);

    const QString thumbnail = thumbnailOf(r);
    if (!thumbnail.isEmpty())
        item.insert(QStringLiteral("thumbnail"), thumbnail);

    for (const QJsonValue &v : r.value(u"buttons").toArray()) {
        const QJsonObject button = v.toObject().value(u"buttonRenderer").toObject();
        const QString icon = at(button, {"icon", "iconType"}).toString();
        Endpoint e = endpointOf(button.value(u"command"));
        if (e.isEmpty())
            e = endpointOf(button.value(u"navigationEndpoint"));
        if (e.playlistId.isEmpty())
            continue;
        if (icon == u"MIX")
            item.insert(QStringLiteral("radioPlaylistId"), e.playlistId);
        else if (icon == u"MUSIC_SHUFFLE")
            item.insert(QStringLiteral("shufflePlaylistId"), e.playlistId);
        else if (icon == u"PLAY_ARROW")
            item.insert(QStringLiteral("playPlaylistId"), e.playlistId);
    }
    applyMenu(item, r.value(u"menu"));
    return item;
}

// musicNavigationButtonRenderer: a mood or genre button on the Explore pages
QVariantMap parseNavigationButton(const QJsonObject &r)
{
    const Endpoint e = endpointOf(r.value(u"clickCommand"));
    if (!e.isBrowse())
        return {};
    QVariantMap item{
        {QStringLiteral("kind"), QStringLiteral("page")},
        {QStringLiteral("title"), textOf(r.value(u"buttonText"))},
        {QStringLiteral("browseId"), e.browseId},
    };
    if (!e.params.isEmpty())
        item.insert(QStringLiteral("params"), e.params);
    // ARGB as an unsigned 32 bit number
    const qint64 stripe = static_cast<qint64>(at(r, {"solid", "leftStripeColor"}).toDouble());
    if (stripe > 0)
        item.insert(QStringLiteral("color"), QStringLiteral("#%1").arg(stripe & 0xffffff, 6, 16, QLatin1Char('0')));
    return item;
}

QVariantList itemsOf(const QJsonArray &contents, bool typeLabel)
{
    QVariantList items;
    for (const QJsonValue &v : contents) {
        QVariantMap item = parseItem(v.toObject(), typeLabel);
        if (!item.isEmpty() && item.contains(QStringLiteral("kind")))
            items.append(item);
    }
    return items;
}

QString commandToken(const QJsonValue &last)
{
    const QString token = at(last, {"continuationItemRenderer", "continuationEndpoint", "continuationCommand",
                                    "token"}).toString();
    if (!token.isEmpty())
        return token;
    for (const QJsonValue &command : at(last, {"continuationItemRenderer", "continuationEndpoint",
                                               "commandExecutorCommand", "commands"}).toArray()) {
        const QString nested = at(command, {"continuationCommand", "token"}).toString();
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}

void continuationOf(const QJsonObject &renderer, const QJsonArray &contents, QString &token, bool &command)
{
    token = at(renderer, {"continuations", 0, "nextContinuationData", "continuation"}).toString();
    command = false;
    if (!token.isEmpty() || contents.isEmpty())
        return;
    token = commandToken(contents.last());
    command = !token.isEmpty();
}

void setShelfContinuation(QVariantMap &shelf, const QJsonObject &renderer, const QJsonArray &contents)
{
    QString token;
    bool command = false;
    continuationOf(renderer, contents, token, command);
    if (token.isEmpty())
        return;
    shelf.insert(QStringLiteral("continuation"), token);
    shelf.insert(QStringLiteral("continuationCommand"), command);
}

QVariantMap moreOf(const QJsonValue &navigationEndpoint)
{
    const Endpoint e = endpointOf(navigationEndpoint);
    if (!e.isBrowse())
        return {};
    return {
        {QStringLiteral("browseId"), e.browseId},
        {QStringLiteral("params"), e.params},
        {QStringLiteral("kind"), kindForPageType(e.pageType)},
    };
}

QVariantMap carouselShelf(const QJsonObject &r, const char *headerKey)
{
    const QJsonObject header = at(r, {"header", headerKey}).toObject();
    const QJsonArray contents = r.value(u"contents").toArray();
    QVariantMap shelf{
        {QStringLiteral("layout"), QStringLiteral("carousel")},
        {QStringLiteral("title"), textOf(header.value(u"title"))},
        {QStringLiteral("strapline"), textOf(header.value(u"strapline"))},
        {QStringLiteral("items"), itemsOf(contents, false)},
    };
    // a carousel of list rows (quick picks) is shown as columns of small rows
    if (!contents.isEmpty() && contents.first().toObject().contains(u"musicResponsiveListItemRenderer"))
        shelf.insert(QStringLiteral("compact"), true);
    if (!contents.isEmpty() && contents.first().toObject().contains(u"musicNavigationButtonRenderer"))
        shelf.insert(QStringLiteral("buttons"), true);
    QVariantMap more = moreOf(at(header, {"moreContentButton", "buttonRenderer", "navigationEndpoint"}));
    if (more.isEmpty())
        more = moreOf(at(header, {"title", "runs", 0, "navigationEndpoint"}));
    if (!more.isEmpty())
        shelf.insert(QStringLiteral("more"), more);
    return shelf;
}

QVariantList parseShelves(const QJsonArray &contents, bool typeLabel)
{
    QVariantList shelves;
    bool lastIsResults = false;

    for (const QJsonValue &v : contents) {
        const QJsonObject o = v.toObject();
        bool results = false;

        if (const QJsonObject r = o.value(u"musicCarouselShelfRenderer").toObject(); !r.isEmpty()) {
            shelves.append(carouselShelf(r, "musicCarouselShelfBasicHeaderRenderer"));
        } else if (const QJsonObject r = o.value(u"musicImmersiveCarouselShelfRenderer").toObject(); !r.isEmpty()) {
            shelves.append(carouselShelf(r, "musicImmersiveCarouselShelfBasicHeaderRenderer"));
        } else if (const QJsonObject r = o.value(u"musicShelfRenderer").toObject(); !r.isEmpty()) {
            const QJsonArray items = r.value(u"contents").toArray();
            QVariantMap shelf{
                {QStringLiteral("layout"), QStringLiteral("list")},
                {QStringLiteral("title"), textOf(r.value(u"title"))},
                {QStringLiteral("items"), itemsOf(items, typeLabel)},
            };
            QVariantMap more = moreOf(at(r, {"bottomEndpoint"}));
            if (more.isEmpty())
                more = moreOf(at(r, {"title", "runs", 0, "navigationEndpoint"}));
            if (!more.isEmpty())
                shelf.insert(QStringLiteral("more"), more);
            setShelfContinuation(shelf, r, items);
            shelves.append(shelf);
        } else if (const QJsonObject r = o.value(u"musicPlaylistShelfRenderer").toObject(); !r.isEmpty()) {
            const QJsonArray items = r.value(u"contents").toArray();
            QVariantMap shelf{
                {QStringLiteral("layout"), QStringLiteral("list")},
                {QStringLiteral("playlistId"), r.value(u"playlistId").toString()},
                {QStringLiteral("items"), itemsOf(items, false)},
            };
            setShelfContinuation(shelf, r, items);
            shelves.append(shelf);
        } else if (const QJsonObject r = o.value(u"musicCardShelfRenderer").toObject(); !r.isEmpty()) {
            shelves.append(QVariantMap{
                {QStringLiteral("layout"), QStringLiteral("card")},
                {QStringLiteral("item"), parseCardItem(r)},
                {QStringLiteral("items"), itemsOf(r.value(u"contents").toArray(), typeLabel)},
            });
        } else if (const QJsonObject r = o.value(u"musicDescriptionShelfRenderer").toObject(); !r.isEmpty()) {
            shelves.append(QVariantMap{
                {QStringLiteral("layout"), QStringLiteral("text")},
                {QStringLiteral("title"), textOf(r.value(u"header"))},
                {QStringLiteral("text"), textOf(r.value(u"description"))},
            });
        } else if (const QJsonObject r = o.value(u"gridRenderer").toObject(); !r.isEmpty()) {
            const QJsonArray items = r.value(u"items").toArray();
            QVariantMap shelf{
                {QStringLiteral("layout"), QStringLiteral("grid")},
                {QStringLiteral("title"), textOf(at(r, {"header", "gridHeaderRenderer", "title"}))},
                {QStringLiteral("items"), itemsOf(items, typeLabel)},
            };
            if (!items.isEmpty() && items.first().toObject().contains(u"musicNavigationButtonRenderer"))
                shelf.insert(QStringLiteral("buttons"), true);
            setShelfContinuation(shelf, r, items);
            shelves.append(shelf);
        } else if (const QJsonObject r = o.value(u"itemSectionRenderer").toObject(); !r.isEmpty()) {
            // the unfiltered search is a flat run of item sections with one row each
            for (const QJsonValue &inner : r.value(u"contents").toArray()) {
                const QVariantMap item = parseItem(inner.toObject(), typeLabel);
                if (item.contains(QStringLiteral("kind"))) {
                    if (lastIsResults) {
                        QVariantMap shelf = shelves.takeLast().toMap();
                        QVariantList items = shelf.value(QStringLiteral("items")).toList();
                        items.append(item);
                        shelf.insert(QStringLiteral("items"), items);
                        shelves.append(shelf);
                    } else {
                        shelves.append(QVariantMap{
                            {QStringLiteral("layout"), QStringLiteral("list")},
                            {QStringLiteral("items"), QVariantList{item}},
                        });
                    }
                    results = true;
                    lastIsResults = true;
                } else {
                    const QVariantList nested = parseShelves(QJsonArray{inner}, typeLabel);
                    shelves.append(nested);
                    if (!nested.isEmpty())
                        lastIsResults = false;
                }
            }
            continue;
        }
        lastIsResults = results;
    }
    return shelves;
}

// the browse endpoint a command runs: itself, or the first of a command list
// (library chips run a browse plus a command that remembers the choice)
QJsonValue browseCommandOf(const QJsonValue &command)
{
    if (command.toObject().contains(u"browseEndpoint"))
        return command;
    for (const QJsonValue &c : at(command, {"commandExecutorCommand", "commands"}).toArray()) {
        if (c.toObject().contains(u"browseEndpoint"))
            return c;
    }
    return {};
}

// chips sit in the section list's header, or on its left side next to a sort menu
QJsonValue chipCloudOf(const QJsonValue &header)
{
    if (header.toObject().contains(u"chipCloudRenderer"))
        return header;
    for (const QJsonValue &v : at(header, {"musicSideAlignedItemRenderer", "startItems"}).toArray()) {
        if (v.toObject().contains(u"chipCloudRenderer"))
            return v;
    }
    return {};
}

QVariantMap parseSort(const QJsonValue &header)
{
    for (const QJsonValue &v : at(header, {"musicSideAlignedItemRenderer", "endItems"}).toArray()) {
        const QJsonValue button = v[u"musicSortFilterButtonRenderer"];
        if (button.isUndefined())
            continue;
        const QString title = textOf(button[u"title"]);
        QVariantList options;
        for (const QJsonValue &o : at(button, {"menu", "musicMultiSelectMenuRenderer", "options"}).toArray()) {
            const QJsonValue option = o[u"musicMultiSelectMenuItemRenderer"];
            // the commands mark the choice in the page's form, then reload the section list
            QString token;
            for (const QJsonValue &c : at(option, {"selectedCommand", "commandExecutorCommand", "commands"}).toArray()) {
                const QString t = at(c, {"browseSectionListReloadEndpoint", "continuation", "reloadContinuationData",
                                         "continuation"}).toString();
                if (!t.isEmpty())
                    token = t;
            }
            const QString name = textOf(option[u"title"]);
            if (name.isEmpty() || token.isEmpty())
                continue;
            options.append(QVariantMap{
                {QStringLiteral("title"), name},
                {QStringLiteral("token"), token},
                {QStringLiteral("selected"), name == title},
            });
        }
        if (!options.isEmpty())
            return {{QStringLiteral("title"), title}, {QStringLiteral("options"), options}};
    }
    return {};
}

QVariantList parseChips(const QJsonValue &header)
{
    QVariantList chips;
    for (const QJsonValue &v : at(chipCloudOf(header), {"chipCloudRenderer", "chips"}).toArray()) {
        const QJsonObject chip = v.toObject().value(u"chipCloudChipRenderer").toObject();
        if (chip.isEmpty())
            continue;
        QVariantMap out{
            {QStringLiteral("title"), textOf(chip.value(u"text"))},
            {QStringLiteral("selected"), chip.value(u"isSelected").toBool()},
        };
        // a chip without text: an icon ("CLOSE" clears the library filter) and its label
        if (const QString icon = at(chip, {"icon", "iconType"}).toString(); !icon.isEmpty()) {
            out.insert(QStringLiteral("icon"), icon.toLower());
            out.insert(QStringLiteral("label"), at(chip, {"accessibilityData", "accessibilityData", "label"}).toString());
        }
        const QJsonObject endpoint = chip.value(u"navigationEndpoint").toObject();
        if (const QJsonObject search = endpoint.value(u"searchEndpoint").toObject(); !search.isEmpty()) {
            out.insert(QStringLiteral("query"), search.value(u"query").toString());
            out.insert(QStringLiteral("params"), search.value(u"params").toString());
        } else {
            const Endpoint e = endpointOf(browseCommandOf(endpoint));
            if (!e.isBrowse())
                continue;
            out.insert(QStringLiteral("browseId"), e.browseId);
            out.insert(QStringLiteral("params"), e.params);
            // a selected library chip leads back to the page without the filter
            const Endpoint back = endpointOf(browseCommandOf(chip.value(u"onDeselectedCommand")));
            if (back.isBrowse())
                out.insert(QStringLiteral("deselectBrowseId"), back.browseId);
        }
        chips.append(out);
    }
    return chips;
}

void applyHeaderButtons(QVariantMap &header, const QJsonArray &buttons)
{
    for (const QJsonValue &v : buttons) {
        const QJsonObject button = v.toObject();
        if (const QJsonObject play = button.value(u"musicPlayButtonRenderer").toObject(); !play.isEmpty()) {
            const Endpoint e = endpointOf(play.value(u"playNavigationEndpoint"));
            if (!e.playlistId.isEmpty())
                header.insert(QStringLiteral("playPlaylistId"), e.playlistId);
            if (e.isWatch())
                header.insert(QStringLiteral("playVideoId"), e.videoId);
        } else if (button.contains(u"menuRenderer")) {
            applyMenu(header, button);
        }
    }
}

// album and playlist pages
QVariantMap parseResponsiveHeader(const QJsonObject &r)
{
    QVariantMap header{
        {QStringLiteral("style"), QStringLiteral("detail")},
        {QStringLiteral("title"), textOf(r.value(u"title"))},
        {QStringLiteral("subtitle"), textOf(r.value(u"subtitle"))},
        {QStringLiteral("strapline"), textOf(r.value(u"straplineTextOne"))},
        {QStringLiteral("secondSubtitle"), textOf(r.value(u"secondSubtitle"))},
        {QStringLiteral("description"),
         textOf(at(r, {"description", "musicDescriptionShelfRenderer", "description"}))},
        {QStringLiteral("thumbnail"), thumbnailOf(r)},
    };
    QVariantMap strapItem;
    applyInfoRuns(strapItem, at(r, {"straplineTextOne", "runs"}).toArray(), false);
    if (strapItem.contains(QStringLiteral("artists")))
        header.insert(QStringLiteral("artists"), strapItem.value(QStringLiteral("artists")));
    applyHeaderButtons(header, r.value(u"buttons").toArray());
    return header;
}

// artist pages
QVariantMap parseImmersiveHeader(const QJsonObject &r)
{
    QVariantMap header{
        {QStringLiteral("style"), QStringLiteral("artist")},
        {QStringLiteral("title"), textOf(r.value(u"title"))},
        {QStringLiteral("description"), textOf(r.value(u"description"))},
        {QStringLiteral("thumbnail"), thumbnailOf(r)},
        {QStringLiteral("subtitle"), textOf(r.value(u"monthlyListenerCount"))},
        {QStringLiteral("subscribers"),
         textOf(at(r, {"subscriptionButton", "subscribeButtonRenderer", "subscriberCountText"}))},
    };
    const Endpoint play = endpointOf(at(r, {"playButton", "buttonRenderer", "navigationEndpoint"}));
    if (!play.playlistId.isEmpty())
        header.insert(QStringLiteral("shufflePlaylistId"), play.playlistId);
    const Endpoint radio = endpointOf(at(r, {"startRadioButton", "buttonRenderer", "navigationEndpoint"}));
    if (!radio.playlistId.isEmpty())
        header.insert(QStringLiteral("radioPlaylistId"), radio.playlistId);
    return header;
}

QVariantMap parseOldDetailHeader(const QJsonObject &r)
{
    return {
        {QStringLiteral("style"), QStringLiteral("detail")},
        {QStringLiteral("title"), textOf(r.value(u"title"))},
        {QStringLiteral("subtitle"), textOf(r.value(u"subtitle"))},
        {QStringLiteral("secondSubtitle"), textOf(r.value(u"secondSubtitle"))},
        {QStringLiteral("description"), textOf(r.value(u"description"))},
        {QStringLiteral("thumbnail"), thumbnailOf(r)},
    };
}

QVariantMap parseHeader(const QJsonObject &o)
{
    if (const QJsonObject r = o.value(u"musicResponsiveHeaderRenderer").toObject(); !r.isEmpty())
        return parseResponsiveHeader(r);
    if (const QJsonObject r = o.value(u"musicEditablePlaylistDetailHeaderRenderer").toObject(); !r.isEmpty()) {
        // the account's own playlist
        const QJsonObject inner = at(r, {"header", "musicResponsiveHeaderRenderer"}).toObject();
        QVariantMap header = !inner.isEmpty() ? parseResponsiveHeader(inner)
                                              : parseOldDetailHeader(at(r, {"header", "musicDetailHeaderRenderer"}).toObject());
        header.insert(QStringLiteral("editable"), true);
        return header;
    }
    if (const QJsonObject r = o.value(u"musicImmersiveHeaderRenderer").toObject(); !r.isEmpty())
        return parseImmersiveHeader(r);
    if (const QJsonObject r = o.value(u"musicVisualHeaderRenderer").toObject(); !r.isEmpty())
        return parseImmersiveHeader(r);
    if (const QJsonObject r = o.value(u"musicDetailHeaderRenderer").toObject(); !r.isEmpty())
        return parseOldDetailHeader(r);
    if (const QJsonObject r = o.value(u"musicHeaderRenderer").toObject(); !r.isEmpty()) {
        return {
            {QStringLiteral("style"), QStringLiteral("title")},
            {QStringLiteral("title"), textOf(r.value(u"title"))},
        };
    }
    return {};
}

void setPageContinuation(Page &page, const QJsonObject &sectionList)
{
    continuationOf(sectionList, sectionList.value(u"contents").toArray(), page.continuation,
                   page.continuationCommand);
}

// playlistPanelVideoRenderer: one entry of the play queue
QVariantMap parseQueueItem(const QJsonObject &r)
{
    if (r.contains(u"unplayableText"))
        return {};
    QVariantMap item;
    const Endpoint e = endpointOf(r.value(u"navigationEndpoint"));
    const QString videoId = r.value(u"videoId").toString();
    if (videoId.isEmpty())
        return {};
    Endpoint watch = e;
    watch.videoId = videoId;
    applyWatch(item, watch, e.videoType);
    item.insert(QStringLiteral("title"), textOf(r.value(u"title")));

    const QJsonArray runs = at(r, {"longBylineText", "runs"}).toArray();
    applyInfoRuns(item, runs, false);
    if (!item.contains(QStringLiteral("artistText")))
        item.insert(QStringLiteral("artistText"), textOf(r.value(u"shortBylineText")));
    item.insert(QStringLiteral("subtitle"), item.value(QStringLiteral("artistText")));

    const QString length = textOf(r.value(u"lengthText"));
    if (isDuration(length))
        setDuration(item, length);
    const QString thumbnail = thumbnailOf(r);
    if (!thumbnail.isEmpty())
        item.insert(QStringLiteral("thumbnail"), thumbnail);
    const QString setVideoId = r.value(u"playlistSetVideoId").toString();
    if (!setVideoId.isEmpty())
        item.insert(QStringLiteral("setVideoId"), setVideoId);
    if (hasExplicitBadge(r.value(u"badges")))
        item.insert(QStringLiteral("explicit"), true);
    applyMenu(item, r.value(u"menu"));
    return item;
}

void parseQueueContents(WatchQueue &queue, const QJsonArray &contents)
{
    for (const QJsonValue &v : contents) {
        QJsonObject o = v.toObject();
        // a song with a video counterpart; the queue plays the primary one
        if (const QJsonObject wrapper = o.value(u"playlistPanelVideoWrapperRenderer").toObject(); !wrapper.isEmpty())
            o = wrapper.value(u"primaryRenderer").toObject();

        if (const QJsonObject r = o.value(u"playlistPanelVideoRenderer").toObject(); !r.isEmpty()) {
            const QVariantMap item = parseQueueItem(r);
            if (item.isEmpty())
                continue;
            if (r.value(u"selected").toBool() && queue.selected < 0)
                queue.selected = static_cast<int>(queue.items.size());
            queue.items.append(item);
        } else if (o.contains(u"automixPreviewVideoRenderer")) {
            const Endpoint e = endpointOf(at(o, {"automixPreviewVideoRenderer", "content",
                                                 "automixPlaylistVideoRenderer", "navigationEndpoint"}));
            queue.automixPlaylistId = e.playlistId;
            queue.automixParams = e.params;
        }
    }
}

QString radioContinuation(const QJsonObject &panel)
{
    const QJsonValue first = at(panel, {"continuations", 0});
    QString token = at(first, {"nextRadioContinuationData", "continuation"}).toString();
    if (token.isEmpty())
        token = at(first, {"nextContinuationData", "continuation"}).toString();
    return token;
}

} // namespace

WatchQueue parseWatchNext(const QJsonObject &response)
{
    WatchQueue queue;
    const QJsonObject tabbed = at(response, {"contents", "singleColumnMusicWatchNextResultsRenderer", "tabbedRenderer",
                                             "watchNextTabbedResultsRenderer"}).toObject();
    for (const QJsonValue &tab : tabbed.value(u"tabs").toArray()) {
        const Endpoint e = endpointOf(at(tab, {"tabRenderer", "endpoint"}));
        if (e.pageType == u"MUSIC_PAGE_TYPE_TRACK_LYRICS")
            queue.lyricsBrowseId = e.browseId;
        else if (e.pageType == u"MUSIC_PAGE_TYPE_TRACK_RELATED")
            queue.relatedBrowseId = e.browseId;
    }

    const QJsonObject panel = at(tabbed, {"tabs", 0, "tabRenderer", "content", "musicQueueRenderer", "content",
                                          "playlistPanelRenderer"}).toObject();
    queue.playlistId = panel.value(u"playlistId").toString();
    parseQueueContents(queue, panel.value(u"contents").toArray());
    queue.continuation = radioContinuation(panel);
    return queue;
}

WatchQueue parseWatchContinuation(const QJsonObject &response)
{
    WatchQueue queue;
    const QJsonObject panel = at(response, {"continuationContents", "playlistPanelContinuation"}).toObject();
    queue.playlistId = panel.value(u"playlistId").toString();
    parseQueueContents(queue, panel.value(u"contents").toArray());
    queue.continuation = radioContinuation(panel);
    return queue;
}

QVariantMap parseItem(const QJsonObject &wrapper, bool typeLabel)
{
    if (const QJsonObject r = wrapper.value(u"musicResponsiveListItemRenderer").toObject(); !r.isEmpty())
        return parseListItem(r, typeLabel);
    if (const QJsonObject r = wrapper.value(u"musicTwoRowItemRenderer").toObject(); !r.isEmpty())
        return parseTwoRowItem(r);
    if (const QJsonObject r = wrapper.value(u"musicMultiRowListItemRenderer").toObject(); !r.isEmpty())
        return parseMultiRowItem(r);
    if (const QJsonObject r = wrapper.value(u"musicNavigationButtonRenderer").toObject(); !r.isEmpty())
        return parseNavigationButton(r);
    return {};
}

Page parseBrowse(const QJsonObject &response, const QString &browseId)
{
    Page page;
    const QJsonObject contents = response.value(u"contents").toObject();

    QJsonObject main;
    QJsonObject secondary;
    if (contents.contains(u"twoColumnBrowseResultsRenderer")) {
        main = at(contents, {"twoColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content",
                             "sectionListRenderer"}).toObject();
        secondary = at(contents, {"twoColumnBrowseResultsRenderer", "secondaryContents",
                                  "sectionListRenderer"}).toObject();
    } else {
        main = at(contents, {"singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content",
                             "sectionListRenderer"}).toObject();
    }

    page.header = parseHeader(response.value(u"header").toObject());

    QJsonArray mainContents;
    for (const QJsonValue &v : main.value(u"contents").toArray()) {
        const QVariantMap header = parseHeader(v.toObject());
        if (!header.isEmpty()) {
            if (page.header.isEmpty())
                page.header = header;
            continue;
        }
        mainContents.append(v);
    }

    page.shelves = parseShelves(mainContents, false);
    page.shelves.append(parseShelves(secondary.value(u"contents").toArray(), false));
    page.chips = parseChips(main.value(u"header"));
    page.sort = parseSort(main.value(u"header"));
    setPageContinuation(page, secondary.isEmpty() ? main : secondary);

    if (!page.header.isEmpty()) {
        QString kind;
        if (browseId.startsWith(u"MPRE"))
            kind = QStringLiteral("album");
        else if (browseId.startsWith(u"VL"))
            kind = QStringLiteral("playlist");
        else if (page.header.value(QStringLiteral("style")).toString() == u"artist")
            kind = QStringLiteral("artist");
        page.header.insert(QStringLiteral("kind"), kind);
        page.header.insert(QStringLiteral("browseId"), browseId);
    }

    // album tracks carry neither cover nor album name; the player needs both
    if (page.header.value(QStringLiteral("kind")).toString() == u"album" && !page.shelves.isEmpty()) {
        QVariantMap shelf = page.shelves.first().toMap();
        if (shelf.value(QStringLiteral("layout")).toString() == u"list") {
            const QVariantMap album{
                {QStringLiteral("name"), page.header.value(QStringLiteral("title"))},
                {QStringLiteral("id"), browseId},
            };
            QVariantList items = shelf.value(QStringLiteral("items")).toList();
            for (QVariant &v : items) {
                QVariantMap item = v.toMap();
                if (!item.contains(QStringLiteral("thumbnail")))
                    item.insert(QStringLiteral("thumbnail"), page.header.value(QStringLiteral("thumbnail")));
                if (!item.contains(QStringLiteral("album")))
                    item.insert(QStringLiteral("album"), album);
                v = item;
            }
            shelf.insert(QStringLiteral("items"), items);
            shelf.insert(QStringLiteral("albumTracks"), true);
            page.shelves.first() = shelf;
        }
    }
    return page;
}

Page parseSearch(const QJsonObject &response)
{
    Page page;
    const QJsonObject section = at(response, {"contents", "tabbedSearchResultsRenderer", "tabs", 0, "tabRenderer",
                                              "content", "sectionListRenderer"}).toObject();
    page.shelves = parseShelves(section.value(u"contents").toArray(), true);
    page.chips = parseChips(section.value(u"header"));
    setPageContinuation(page, section);
    return page;
}

Page parseContinuation(const QJsonObject &response)
{
    Page page;
    const QJsonObject contents = response.value(u"continuationContents").toObject();

    if (const QJsonObject r = contents.value(u"sectionListContinuation").toObject(); !r.isEmpty()) {
        page.shelves = parseShelves(r.value(u"contents").toArray(), false);
        // a reload (another sort order) brings its chips and menu along
        page.chips = parseChips(r.value(u"header"));
        page.sort = parseSort(r.value(u"header"));
        setPageContinuation(page, r);
        return page;
    }
    for (const char *key : {"musicShelfContinuation", "musicPlaylistShelfContinuation", "gridContinuation"}) {
        const QJsonObject r = contents.value(QLatin1StringView(key)).toObject();
        if (r.isEmpty())
            continue;
        const QJsonArray items = r.contains(u"items") ? r.value(u"items").toArray() : r.value(u"contents").toArray();
        page.items = itemsOf(items, true);
        continuationOf(r, items, page.continuation, page.continuationCommand);
        return page;
    }

    const QJsonArray items = at(response, {"onResponseReceivedActions", 0, "appendContinuationItemsAction",
                                           "continuationItems"}).toArray();
    if (!items.isEmpty()) {
        const QJsonObject first = items.first().toObject();
        const bool shelves = !first.isEmpty() && first.constBegin().key().endsWith(u"ShelfRenderer");
        if (shelves)
            page.shelves = parseShelves(items, false);
        else
            page.items = itemsOf(items, false);
        page.continuation = commandToken(items.last());
        page.continuationCommand = true;
    }
    return page;
}

QVariantMap parseSuggestions(const QJsonObject &response)
{
    QVariantList queries;
    QVariantList items;
    for (const QJsonValue &section : response.value(u"contents").toArray()) {
        for (const QJsonValue &v : at(section, {"searchSuggestionsSectionRenderer", "contents"}).toArray()) {
            const QJsonObject o = v.toObject();
            if (const QJsonObject s = o.value(u"searchSuggestionRenderer").toObject(); !s.isEmpty()) {
                queries.append(QVariantMap{
                    {QStringLiteral("text"), textOf(s.value(u"suggestion"))},
                    {QStringLiteral("query"), at(s, {"navigationEndpoint", "searchEndpoint", "query"}).toString()},
                });
            } else {
                const QVariantMap item = parseItem(o, true);
                if (item.contains(QStringLiteral("kind")))
                    items.append(item);
            }
        }
    }
    return {{QStringLiteral("queries"), queries}, {QStringLiteral("items"), items}};
}

QStringList artistNames(const QVariantMap &item)
{
    QStringList names;
    for (const QVariant &a : item.value(QStringLiteral("artists")).toList())
        names.append(a.toMap().value(QStringLiteral("name")).toString());
    const QString text = item.value(QStringLiteral("artistText")).toString();
    if (names.isEmpty() && !text.isEmpty())
        names.append(text);
    return names;
}

QString artUrl(const QString &url, int pixels)
{
    static const QRegularExpression sized(QStringLiteral("=w(\\d+)-h(\\d+)([^/=]*)$"));
    if (pixels <= 0 || !(url.contains(u"googleusercontent.com") || url.contains(u"ggpht.com")))
        return url;
    const QRegularExpressionMatch m = sized.match(url);
    if (!m.hasMatch() || m.captured(1) != m.captured(2))
        return url;
    QString out = url;
    out.replace(m.capturedStart(), m.capturedLength(),
                QStringLiteral("=w%1-h%1%2").arg(pixels).arg(m.captured(3)));
    return out;
}

} // namespace ytm
