#include "ytm/BrowsePage.h"
#include "app/AppController.h"
#include "ytm/InnerTube.h"
#include "ytm/Parser.h"

#include <QDir>
#include <QLoggingCategory>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcPage, "quince.page")

namespace {

const QString kHome = QStringLiteral("FEmusic_home");

QString homeCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/home.json");
}

} // namespace

BrowsePage::BrowsePage(QObject *parent)
    : QAbstractListModel(parent)
{
}

void BrowsePage::setBrowseId(const QString &id)
{
    if (m_browseId == id)
        return;
    m_browseId = id;
    emit requestChanged();
    if (m_complete)
        reload();
}

void BrowsePage::setParams(const QString &params)
{
    if (m_params == params)
        return;
    m_params = params;
    emit requestChanged();
    if (m_complete)
        reload();
}

void BrowsePage::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit requestChanged();
    if (m_complete)
        reload();
}

int BrowsePage::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant BrowsePage::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RowTypeRole:
        return row.type;
    case RowRole:
        return row.data;
    default:
        return {};
    }
}

QHash<int, QByteArray> BrowsePage::roleNames() const
{
    return {
        {RowTypeRole, QByteArrayLiteral("rowType")},
        {RowRole, QByteArrayLiteral("row")},
    };
}

void BrowsePage::componentComplete()
{
    m_complete = true;
    reload();
}

bool BrowsePage::isHome() const
{
    return m_query.isEmpty() && m_params.isEmpty() && (m_browseId.isEmpty() || m_browseId == kHome);
}

QString BrowsePage::endpoint() const
{
    return m_query.isEmpty() ? QStringLiteral("browse") : QStringLiteral("search");
}

QJsonObject BrowsePage::body() const
{
    QJsonObject body;
    if (m_query.isEmpty())
        body.insert(QStringLiteral("browseId"), m_browseId.isEmpty() ? kHome : m_browseId);
    else
        body.insert(QStringLiteral("query"), m_query);
    if (!m_params.isEmpty())
        body.insert(QStringLiteral("params"), m_params);
    return body;
}

void BrowsePage::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void BrowsePage::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void BrowsePage::reload()
{
    const int generation = ++m_generation;
    setError({});

    // the last home page is shown at once and replaced when the new one arrives
    if (isHome() && m_rows.isEmpty())
        loadCachedHome();

    setLoading(true);
    const bool search = !m_query.isEmpty();
    const QString browseId = m_browseId;
    const bool home = isHome();
    AppController::instance()->innerTube()->post(
        endpoint(), body(), this,
        [this, generation, search, browseId, home](const QJsonObject &response, const QString &error) {
            if (generation != m_generation)
                return;
            setLoading(false);
            if (!error.isEmpty()) {
                setError(error);
                return;
            }
            applyPage(search ? ytm::parseSearch(response) : ytm::parseBrowse(response, browseId), true);
            qCDebug(lcPage) << (search ? "search" : browseId) << "loaded:" << m_rows.size() << "rows";
            if (home)
                saveCachedHome(response);
        });
}

void BrowsePage::loadMore()
{
    if (!canLoadMore())
        return;
    const Pending pending = m_pending.takeFirst();
    const int generation = m_generation;
    setLoading(true);
    AppController::instance()->innerTube()->continuation(
        endpoint(), body(), pending.token, pending.command, this,
        [this, generation, pending](const QJsonObject &response, const QString &error) {
            if (generation != m_generation)
                return;
            setLoading(false);
            if (!error.isEmpty()) {
                // keep the token so scrolling down again retries
                m_pending.prepend(pending);
                setError(error);
                return;
            }
            const ytm::Page page = ytm::parseContinuation(response);
            if (!page.items.isEmpty() && pending.shelf >= 0) {
                appendItems(pending.shelf, page.items);
                if (!page.continuation.isEmpty())
                    m_pending.prepend({pending.shelf, page.continuation, page.continuationCommand});
            } else {
                applyPage(page, false);
            }
            emit loadingChanged();
        });
}

void BrowsePage::sortBy(const QString &token)
{
    if (token.isEmpty())
        return;
    const int generation = ++m_generation;
    setError({});
    setLoading(true);
    AppController::instance()->innerTube()->continuation(
        endpoint(), body(), token, true, this,
        [this, generation](const QJsonObject &response, const QString &error) {
            if (generation != m_generation)
                return;
            setLoading(false);
            if (!error.isEmpty()) {
                setError(error);
                return;
            }
            ytm::Page page = ytm::parseContinuation(response);
            // an answer of another shape leaves the page as it was
            if (page.shelves.isEmpty()) {
                qCWarning(lcPage) << "sort: no sections in the answer";
                return;
            }
            // what the reload leaves out stays as it was
            page.header = m_header;
            if (page.chips.isEmpty())
                page.chips = m_chips;
            applyPage(page, true);
        });
}

void BrowsePage::applyPage(const ytm::Page &page, bool replace)
{
    QList<Row> rows;
    if (replace) {
        m_shelves.clear();
        m_pending.clear();
        m_header = page.header;
        m_chips = page.chips;
        m_sort = page.sort;
        emit headerChanged();

        if (!m_header.isEmpty())
            rows.append({QStringLiteral("header"), m_header, -1});
        if (!m_chips.isEmpty() || !m_sort.isEmpty())
            rows.append({QStringLiteral("chips"), {{QStringLiteral("chips"), m_chips}, {QStringLiteral("sort"), m_sort}},
                         -1});
    }

    appendShelves(page.shelves, rows);
    if (!page.continuation.isEmpty())
        m_pending.append({-1, page.continuation, page.continuationCommand});

    if (replace) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    } else if (!rows.isEmpty()) {
        beginInsertRows({}, static_cast<int>(m_rows.size()), static_cast<int>(m_rows.size() + rows.size() - 1));
        m_rows.append(rows);
        endInsertRows();
    }
    emit loadingChanged();
}

void BrowsePage::appendShelves(const QVariantList &shelves, QList<Row> &rows)
{
    for (const QVariant &v : shelves) {
        QVariantMap shelf = v.toMap();
        const int index = static_cast<int>(m_shelves.size());
        shelf.insert(QStringLiteral("shelf"), index);
        m_shelves.append(shelf);

        const QString layout = shelf.value(QStringLiteral("layout")).toString();
        if (layout == u"list") {
            const QString title = shelf.value(QStringLiteral("title")).toString();
            if (!title.isEmpty())
                rows.append({QStringLiteral("title"), shelf, index});
            const bool albumTracks = shelf.value(QStringLiteral("albumTracks")).toBool();
            int i = 0;
            for (const QVariant &item : shelf.value(QStringLiteral("items")).toList()) {
                rows.append({QStringLiteral("item"),
                             {{QStringLiteral("item"), item},
                              {QStringLiteral("shelf"), index},
                              {QStringLiteral("index"), i++},
                              {QStringLiteral("albumTracks"), albumTracks}},
                             index});
            }
            const QString token = shelf.value(QStringLiteral("continuation")).toString();
            if (!token.isEmpty())
                m_pending.append({index, token, shelf.value(QStringLiteral("continuationCommand")).toBool()});
        } else if (layout == u"card") {
            rows.append({QStringLiteral("card"), shelf, index});
            int i = 0;
            for (const QVariant &item : shelf.value(QStringLiteral("items")).toList()) {
                rows.append({QStringLiteral("item"),
                             {{QStringLiteral("item"), item},
                              {QStringLiteral("shelf"), index},
                              {QStringLiteral("index"), i++}},
                             index});
            }
        } else if (layout == u"carousel" || layout == u"grid" || layout == u"text") {
            if (!shelf.value(QStringLiteral("items")).toList().isEmpty() || layout == u"text")
                rows.append({layout, shelf, index});
        }
    }
}

void BrowsePage::appendItems(int shelf, const QVariantList &items)
{
    if (shelf < 0 || shelf >= m_shelves.size() || items.isEmpty())
        return;

    QVariantMap shelfMap = m_shelves.at(shelf).toMap();
    QVariantList all = shelfMap.value(QStringLiteral("items")).toList();
    int index = static_cast<int>(all.size());
    all.append(items);
    shelfMap.insert(QStringLiteral("items"), all);
    m_shelves[shelf] = shelfMap;

    // after the last row of that shelf, other shelves may follow it
    int at = static_cast<int>(m_rows.size());
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (m_rows.at(i).shelf == shelf) {
            at = i + 1;
            break;
        }
    }

    QList<Row> rows;
    for (const QVariant &item : items) {
        rows.append({QStringLiteral("item"),
                     {{QStringLiteral("item"), item},
                      {QStringLiteral("shelf"), shelf},
                      {QStringLiteral("index"), index++}},
                     shelf});
    }
    beginInsertRows({}, at, at + static_cast<int>(rows.size()) - 1);
    for (qsizetype i = 0; i < rows.size(); ++i)
        m_rows.insert(at + i, rows.at(i));
    endInsertRows();
}

QVariantList BrowsePage::shelfItems(int shelf) const
{
    if (shelf < 0 || shelf >= m_shelves.size())
        return {};
    return m_shelves.at(shelf).toMap().value(QStringLiteral("items")).toList();
}

QStringList BrowsePage::trackIds() const
{
    QStringList ids;
    for (const Row &row : m_rows) {
        if (row.type != u"item")
            continue;
        const QString id = row.data.value(QStringLiteral("item")).toMap().value(QStringLiteral("videoId")).toString();
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

void BrowsePage::loadCachedHome()
{
    QFile file(homeCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject response = QJsonDocument::fromJson(file.readAll()).object();
    if (!response.isEmpty()) {
        applyPage(ytm::parseBrowse(response, kHome), true);
        qCDebug(lcPage) << "home from cache:" << m_rows.size() << "rows";
    }
}

void BrowsePage::saveCachedHome(const QJsonObject &response) const
{
    const QString path = homeCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    file.commit();
}
