#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QQmlParserStatus>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace ytm {
struct Page;
}

// One YouTube Music page (home, a browse page or a search) as a flat list of
// rows for a ListView: header, chips, shelf titles, one row per list item and
// one row per carousel. Flat rows keep a 5000 track playlist virtualized.
//
// rowType: header, chips, title, item, carousel, grid, card, text
class BrowsePage : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QString browseId READ browseId WRITE setBrowseId NOTIFY requestChanged)
    Q_PROPERTY(QString params READ params WRITE setParams NOTIFY requestChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY requestChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap header READ header NOTIFY headerChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY loadingChanged)

public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1,
        RowRole,
    };

    explicit BrowsePage(QObject *parent = nullptr);

    QString browseId() const { return m_browseId; }
    QString params() const { return m_params; }
    QString query() const { return m_query; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QVariantMap header() const { return m_header; }
    bool canLoadMore() const { return !m_pending.isEmpty() && !m_loading; }

    void setBrowseId(const QString &id);
    void setParams(const QString &params);
    void setQuery(const QString &query);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void classBegin() override {}
    void componentComplete() override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // all items of a shelf, e.g. to queue an album from the track that was clicked
    Q_INVOKABLE QVariantList shelfItems(int shelf) const;

signals:
    void requestChanged();
    void loadingChanged();
    void errorChanged();
    void headerChanged();

private:
    struct Row
    {
        QString type;
        QVariantMap data;
        int shelf = -1;
    };
    struct Pending
    {
        int shelf = -1; // list shelf whose items continue; -1 continues the shelves
        QString token;
        bool command = false;
    };

    bool isHome() const;
    QString endpoint() const;
    QJsonObject body() const;
    void applyPage(const ytm::Page &page, bool replace);
    void appendShelves(const QVariantList &shelves, QList<Row> &rows);
    void appendItems(int shelf, const QVariantList &items);
    void setLoading(bool loading);
    void setError(const QString &error);
    void loadCachedHome();
    void saveCachedHome(const QJsonObject &response) const;

    QString m_browseId;
    QString m_params;
    QString m_query;
    bool m_complete = false;
    bool m_loading = false;
    QString m_error;
    int m_generation = 0;

    QVariantMap m_header;
    QVariantList m_chips;
    QVariantList m_shelves;
    QList<Row> m_rows;
    QList<Pending> m_pending;
};
