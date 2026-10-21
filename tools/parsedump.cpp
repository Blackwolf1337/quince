// Prints what the parser makes of a saved InnerTube answer (development aid).
// usage: quince-parsedump browse|search|continuation|suggest|next|nextmore <answer.json> [browseId]
#include "ytm/Parser.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        QTextStream(stderr) << "usage: quince-parsedump browse|search|continuation|suggest|next|nextmore <answer.json> [browseId]\n";
        return 2;
    }
    QFile file(args[2]);
    if (!file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "cannot open " << args[2] << "\n";
        return 1;
    }
    const QJsonObject response = QJsonDocument::fromJson(file.readAll()).object();

    QVariantMap out;
    if (args[1] == u"suggest") {
        out = ytm::parseSuggestions(response);
    } else if (args[1] == u"next" || args[1] == u"nextmore") {
        const ytm::WatchQueue queue = args[1] == u"next" ? ytm::parseWatchNext(response)
                                                         : ytm::parseWatchContinuation(response);
        out = {
            {QStringLiteral("items"), queue.items},
            {QStringLiteral("selected"), queue.selected},
            {QStringLiteral("playlistId"), queue.playlistId},
            {QStringLiteral("continuation"), queue.continuation.left(24)},
            {QStringLiteral("automix"), queue.automixPlaylistId},
            {QStringLiteral("lyrics"), queue.lyricsBrowseId},
            {QStringLiteral("related"), queue.relatedBrowseId},
        };
    } else {
        ytm::Page page;
        if (args[1] == u"browse")
            page = ytm::parseBrowse(response, args.value(3));
        else if (args[1] == u"search")
            page = ytm::parseSearch(response);
        else
            page = ytm::parseContinuation(response);
        out = {
            {QStringLiteral("header"), page.header},
            {QStringLiteral("chips"), page.chips},
            {QStringLiteral("shelves"), page.shelves},
            {QStringLiteral("items"), page.items},
            {QStringLiteral("continuation"), page.continuation.left(24)},
            {QStringLiteral("continuationCommand"), page.continuationCommand},
        };
    }
    QTextStream(stdout) << QJsonDocument::fromVariant(out).toJson(QJsonDocument::Indented);
    return 0;
}
