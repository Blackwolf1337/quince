#pragma once

// A Slint struct whose fields mirror Q_PROPERTYs of a QObject (Settings,
// AudioEffects): a table of (property name, field) instead of a getter and a
// setter per property. read() and write() run on the core thread and go
// through QMetaProperty by name; fill() and collect() run on the Slint
// thread. Values cross as Value, a plain copy.
//
// Field types: bool, int (also for double properties shown as whole
// numbers), float (double properties) and string (QString, or a QStringList
// as ",a,b,", which Slint can search with replace-all()).

#include "Bridge.h"

#include <QObject>
#include <QStringList>
#include <QVariant>

#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace props {

template<typename Struct>
using Member = std::variant<bool Struct::*, int Struct::*, float Struct::*, slint::SharedString Struct::*>;

template<typename Struct>
struct Field
{
    const char *property;
    Member<Struct> member;
};

// the same order of types as Member
using Value = std::variant<bool, int, double, std::string>;

inline QString qstr(const slint::SharedString &s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

namespace detail {

inline Value toValue(const QVariant &v, std::type_identity<bool>)
{
    return v.toBool();
}

inline Value toValue(const QVariant &v, std::type_identity<int>)
{
    return qRound(v.toDouble());
}

inline Value toValue(const QVariant &v, std::type_identity<float>)
{
    return v.toDouble();
}

inline Value toValue(const QVariant &v, std::type_identity<slint::SharedString>)
{
    if (v.metaType().id() == QMetaType::QStringList) {
        const QStringList list = v.toStringList();
        return bridge::utf8(list.isEmpty() ? QStringLiteral(",") : u',' + list.join(u',') + u',');
    }
    return bridge::utf8(v.toString());
}

inline QVariant toVariant(const Value &value, const QVariant &current)
{
    return std::visit([&current](const auto &v) -> QVariant {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            const QString text = QString::fromStdString(v);
            if (current.metaType().id() == QMetaType::QStringList)
                return text.split(u',', Qt::SkipEmptyParts);
            return text;
        } else
            return QVariant::fromValue(v);
    }, value);
}

// floats went through Slint: compare at its precision
inline bool same(const Value &a, const Value &b)
{
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b))
        return float(std::get<double>(a)) == float(std::get<double>(b));
    return a == b;
}

} // namespace detail

// core thread
template<typename Struct>
std::vector<Value> read(const QObject *obj, std::span<const Field<Struct>> fields)
{
    std::vector<Value> values;
    values.reserve(fields.size());
    for (const Field<Struct> &f : fields) {
        const QVariant v = obj->property(f.property);
        values.push_back(std::visit([&v](auto member) {
            using T = std::remove_cvref_t<decltype(std::declval<Struct &>().*member)>;
            return detail::toValue(v, std::type_identity<T>{});
        }, f.member));
    }
    return values;
}

// core thread: writes the values that differ from the object's
template<typename Struct>
void write(QObject *obj, std::span<const Field<Struct>> fields, const std::vector<Value> &values)
{
    const std::vector<Value> current = read(obj, fields);
    for (size_t i = 0; i < fields.size() && i < values.size(); i++)
        if (!detail::same(current[i], values[i]))
            obj->setProperty(fields[i].property, detail::toVariant(values[i], obj->property(fields[i].property)));
}

// Slint thread
template<typename Struct>
void fill(Struct &s, std::span<const Field<Struct>> fields, const std::vector<Value> &values)
{
    for (size_t i = 0; i < fields.size() && i < values.size(); i++) {
        std::visit([&s, &value = values[i]](auto member) {
            using T = std::decay_t<decltype(s.*member)>;
            if constexpr (std::is_same_v<T, slint::SharedString>)
                s.*member = bridge::str(std::get<std::string>(value));
            else if constexpr (std::is_same_v<T, float>)
                s.*member = float(std::get<double>(value));
            else
                s.*member = std::get<T>(value);
        }, fields[i].member);
    }
}

// Slint thread
template<typename Struct>
std::vector<Value> collect(const Struct &s, std::span<const Field<Struct>> fields)
{
    std::vector<Value> values;
    values.reserve(fields.size());
    for (const Field<Struct> &f : fields) {
        values.push_back(std::visit([&s](auto member) -> Value {
            using T = std::decay_t<decltype(s.*member)>;
            if constexpr (std::is_same_v<T, slint::SharedString>)
                return std::string(std::string_view(s.*member));
            else if constexpr (std::is_same_v<T, float>)
                return double(s.*member);
            else
                return s.*member;
        }, f.member));
    }
    return values;
}

} // namespace props
