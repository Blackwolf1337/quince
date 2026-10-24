#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Turns InnerTube answers into plain maps for the user interface.
//
// The type of an item comes from its endpoint (page type of a browse
// endpoint, music video type of a watch endpoint), never from the localized
// text YouTube shows ("Album", "Titel", ...).
//
// Item keys: kind (song, video, episode, album, artist, playlist, podcast,
// station, page), title, subtitle, typeLabel, artistText, artists [{name, id}],
// album {name, id}, year, duration, durationSeconds, thumbnail, wide, explicit,
// index, videoId, playlistId, videoType, browseId, params, playPlaylistId,
// radioPlaylistId, shufflePlaylistId, setVideoId, unavailable, likeStatus
// (LIKE, DISLIKE, INDIFFERENT; only when signed in), owned (own playlist).
//
// Shelf keys: layout (list, carousel, grid, card, text), title, strapline,
// more {browseId, params, kind}, items, item (card), text, continuation,
// continuationCommand.
namespace ytm {

struct Page
{
    QVariantMap header;
    QVariantList chips;
    // a sort menu (library pages): title (the order in use), options [{title, token, selected}];
    // an option's token reloads the page through BrowsePage::sortBy
    QVariantMap sort;
    QVariantList shelves;
    QVariantList items; // continuation of a list shelf

    QString continuation;
    bool continuationCommand = false;
};

// What the next endpoint returns: the play queue.
struct WatchQueue
{
    QVariantList items;
    int selected = -1;
    QString playlistId;
    QString continuation; // radio: more items with the same request
    QString automixPlaylistId; // played after the last item
    QString automixParams;
    QString lyricsBrowseId; // of the requested track
    QString relatedBrowseId;
};

QVariantMap parseItem(const QJsonObject &wrapper, bool typeLabel);

Page parseBrowse(const QJsonObject &response, const QString &browseId);
Page parseSearch(const QJsonObject &response);
Page parseContinuation(const QJsonObject &response);

WatchQueue parseWatchNext(const QJsonObject &response);
WatchQueue parseWatchContinuation(const QJsonObject &response);

// {queries: [{text, query}], items: [item]}
QVariantMap parseSuggestions(const QJsonObject &response);

// Square googleusercontent pictures are served in any size; others unchanged.
QString artUrl(const QString &url, int pixels);

} // namespace ytm
