#include "player/MpvNode.h"

#include <QVariantList>
#include <QVariantMap>

MpvNode::MpvNode(const QVariant &value)
{
    build(m_root, value);
}

char *MpvNode::store(const QString &text)
{
    m_strings.push_back(text.toUtf8());
    return m_strings.back().data();
}

void MpvNode::build(mpv_node &dst, const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        dst.format = MPV_FORMAT_FLAG;
        dst.u.flag = value.toBool() ? 1 : 0;
        return;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        dst.format = MPV_FORMAT_INT64;
        dst.u.int64 = value.toLongLong();
        return;
    case QMetaType::Double:
    case QMetaType::Float:
        dst.format = MPV_FORMAT_DOUBLE;
        dst.u.double_ = value.toDouble();
        return;
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        const QVariantList items = value.toList();
        auto &values = m_values.emplace_back(items.size());
        auto &list = m_lists.emplace_back();
        list.num = static_cast<int>(items.size());
        list.values = values.data();
        list.keys = nullptr;
        for (qsizetype i = 0; i < items.size(); ++i)
            build(values[i], items[i]);
        dst.format = MPV_FORMAT_NODE_ARRAY;
        dst.u.list = &list;
        return;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap items = value.toMap();
        auto &values = m_values.emplace_back(items.size());
        auto &keys = m_keys.emplace_back(items.size());
        auto &list = m_lists.emplace_back();
        list.num = static_cast<int>(items.size());
        list.values = values.data();
        list.keys = keys.data();
        qsizetype i = 0;
        for (auto it = items.cbegin(); it != items.cend(); ++it, ++i) {
            keys[i] = store(it.key());
            build(values[i], it.value());
        }
        dst.format = MPV_FORMAT_NODE_MAP;
        dst.u.list = &list;
        return;
    }
    default:
        if (!value.isValid()) {
            dst.format = MPV_FORMAT_NONE;
            return;
        }
        dst.format = MPV_FORMAT_STRING;
        dst.u.string = store(value.toString());
        return;
    }
}

QVariant MpvNode::toVariant(const mpv_node *node)
{
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return node->u.flag != 0;
    case MPV_FORMAT_INT64:
        return static_cast<qlonglong>(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList out;
        for (int i = 0; i < node->u.list->num; ++i)
            out.append(toVariant(&node->u.list->values[i]));
        return out;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap out;
        for (int i = 0; i < node->u.list->num; ++i)
            out.insert(QString::fromUtf8(node->u.list->keys[i]),
                       toVariant(&node->u.list->values[i]));
        return out;
    }
    default:
        return {};
    }
}
