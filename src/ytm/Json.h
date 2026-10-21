#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>

#include <initializer_list>

namespace ytm {

// One step of a path into an InnerTube answer: an object key or an array index.
struct Key
{
    Key(const char *name)
        : name(name)
    {
    }
    Key(int index)
        : index(index)
    {
    }

    QLatin1StringView name;
    int index = -1;
};

// Follows path from value; Undefined as soon as a step is missing.
inline QJsonValue at(QJsonValue value, std::initializer_list<Key> path)
{
    for (const Key &key : path) {
        if (key.index >= 0) {
            const QJsonArray array = value.toArray();
            if (key.index >= array.size())
                return QJsonValue(QJsonValue::Undefined);
            value = array.at(key.index);
        } else {
            value = value.toObject().value(key.name);
        }
        if (value.isUndefined())
            return value;
    }
    return value;
}

// Text of a {"runs": [...]} or {"simpleText": ...} object.
inline QString textOf(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    if (const QJsonValue simple = object.value(QLatin1StringView("simpleText")); simple.isString())
        return simple.toString();
    QString out;
    for (const QJsonValue &run : object.value(QLatin1StringView("runs")).toArray())
        out += run.toObject().value(QLatin1StringView("text")).toString();
    return out;
}

} // namespace ytm
