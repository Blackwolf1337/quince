#include "player/Queue.h"

#include <QRandomGenerator>

#include <algorithm>

namespace {

const QString kVideoId = QStringLiteral("videoId");

} // namespace

Queue::Queue(QObject *parent)
    : QAbstractListModel(parent)
{
}

QVariantMap Queue::at(int index) const
{
    if (index < 0 || index >= m_items.size())
        return {};
    return m_items.at(index).toMap();
}

QString Queue::videoIdAt(int index) const
{
    return at(index).value(kVideoId).toString();
}

int Queue::indexOf(const QString &videoId, int from) const
{
    for (int i = qMax(0, from); i < m_items.size(); ++i) {
        if (m_items.at(i).toMap().value(kVideoId).toString() == videoId)
            return i;
    }
    return -1;
}

void Queue::setItems(const QVariantList &items, int current)
{
    beginResetModel();
    m_items = items;
    m_current = items.isEmpty() ? -1 : qBound(0, current, static_cast<int>(items.size()) - 1);
    endResetModel();
    emit countChanged();
    emit currentChanged();
    emit upcomingChanged();
}

void Queue::setCurrent(int index)
{
    if (index < 0 || index >= m_items.size() || index == m_current)
        return;
    const int before = m_current;
    m_current = index;
    notifyCurrentRows(before, index);
    emit currentChanged();
    emit upcomingChanged();
}

void Queue::notifyCurrentRows(int before, int after)
{
    if (before >= 0 && before < m_items.size())
        emit dataChanged(createIndex(before, 0), createIndex(before, 0), {CurrentRole});
    if (after >= 0 && after < m_items.size())
        emit dataChanged(createIndex(after, 0), createIndex(after, 0), {CurrentRole});
}

void Queue::append(const QVariantList &items)
{
    if (items.isEmpty())
        return;
    const bool upcoming = m_current == m_items.size() - 1;
    beginInsertRows({}, static_cast<int>(m_items.size()), static_cast<int>(m_items.size() + items.size() - 1));
    m_items.append(items);
    endInsertRows();
    if (m_current < 0) {
        m_current = 0;
        emit currentChanged();
    }
    emit countChanged();
    if (upcoming)
        emit upcomingChanged();
}

void Queue::insert(int index, const QVariantMap &item)
{
    index = qBound(0, index, static_cast<int>(m_items.size()));
    beginInsertRows({}, index, index);
    m_items.insert(index, item);
    endInsertRows();
    if (m_current < 0) {
        m_current = 0;
        emit currentChanged();
    } else if (index <= m_current) {
        ++m_current;
        emit currentChanged();
    }
    emit countChanged();
    if (index == m_current + 1)
        emit upcomingChanged();
}

void Queue::replace(int index, const QVariantMap &item)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_items[index] = item;
    emit dataChanged(createIndex(index, 0), createIndex(index, 0), {ItemRole});
}

void Queue::remove(int index)
{
    if (index < 0 || index >= m_items.size() || index == m_current)
        return;
    beginRemoveRows({}, index, index);
    m_items.removeAt(index);
    endRemoveRows();
    if (index < m_current) {
        --m_current;
        emit currentChanged();
    }
    emit countChanged();
    if (index == m_current + 1)
        emit upcomingChanged();
}

void Queue::move(int from, int to)
{
    if (from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size() || from == to)
        return;
    const QString upcomingBefore = videoIdAt(m_current + 1);
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_items.move(from, to);
    endMoveRows();

    int current = m_current;
    if (from == m_current)
        current = to;
    else if (from < m_current && to >= m_current)
        --current;
    else if (from > m_current && to <= m_current)
        ++current;
    if (current != m_current) {
        m_current = current;
        emit currentChanged();
    }
    if (videoIdAt(m_current + 1) != upcomingBefore)
        emit upcomingChanged();
}

void Queue::shuffleRest()
{
    if (m_current + 2 >= m_items.size())
        return;
    beginResetModel();
    std::shuffle(m_items.begin() + m_current + 1, m_items.end(), *QRandomGenerator::global());
    endResetModel();
    emit upcomingChanged();
}

void Queue::clearRest()
{
    if (m_current < 0)
        return;
    const QVariant keep = m_items.at(m_current);
    setItems({keep}, 0);
}

int Queue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant Queue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    switch (role) {
    case ItemRole:
        return m_items.at(index.row());
    case CurrentRole:
        return index.row() == m_current;
    default:
        return {};
    }
}

QHash<int, QByteArray> Queue::roleNames() const
{
    return {
        {ItemRole, QByteArrayLiteral("item")},
        {CurrentRole, QByteArrayLiteral("isCurrent")},
    };
}
