#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// The play queue: page items (maps, see ytm/Parser.h) and which one plays.
class Queue : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The queue is owned by App")

    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        ItemRole = Qt::UserRole + 1,
        CurrentRole,
    };

    explicit Queue(QObject *parent = nullptr);

    int current() const { return m_current; }
    int count() const { return static_cast<int>(m_items.size()); }
    QVariantMap at(int index) const;
    QVariantMap currentItem() const { return at(m_current); }
    QString videoIdAt(int index) const;
    QVariantList items() const { return m_items; }
    int indexOf(const QString &videoId, int from = 0) const;

    void setItems(const QVariantList &items, int current);
    void setCurrent(int index);
    void append(const QVariantList &items);
    void insert(int index, const QVariantMap &item);
    void replace(int index, const QVariantMap &item);

    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    // keeps the playing item, shuffles the ones after it
    Q_INVOKABLE void shuffleRest();
    // drops everything but the playing item
    Q_INVOKABLE void clearRest();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void currentChanged();
    void countChanged();
    // the item after the current one is a different one now
    void upcomingChanged();

private:
    void notifyCurrentRows(int before, int after);

    QVariantList m_items;
    int m_current = -1;
};
