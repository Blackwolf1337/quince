#pragma once

// Crossing between the core thread (QCoreApplication, AppController and
// everything it owns) and the Slint thread (the main thread: windows, models,
// images). Values cross as plain C++ copies, never as Qt or Slint objects.

#include "main.h"

#include <QMetaObject>
#include <QObject>
#include <QString>

#include <string>
#include <utility>

namespace bridge {

using Window = slint::ComponentWeakHandle<MainWindow>;
using Tray = slint::ComponentWeakHandle<TrayIcon>;

// f(component) on the Slint thread, if the component still exists
template<typename Component, typename F>
void toUi(const slint::ComponentWeakHandle<Component> &component, F &&f)
{
    slint::invoke_from_event_loop([component, f = std::forward<F>(f)]() mutable {
        if (auto c = component.lock())
            f(**c);
    });
}

// f() on the thread of obj (the core thread)
template<typename Obj, typename F>
void toCore(Obj *obj, F &&f)
{
    QMetaObject::invokeMethod(obj, std::forward<F>(f), Qt::QueuedConnection);
}

// Keeps a Slint property in step with a Qt one: read(obj) runs on the core
// thread now and on every change signal, write(component, value) on the
// Slint thread with a copy of what read() returned.
template<typename Component, typename Obj, typename Signal, typename Read, typename Write>
void bind(const slint::ComponentWeakHandle<Component> &component, Obj *obj, Signal signal, Read read, Write write)
{
    auto push = [component, obj, read, write] {
        toUi(component, [value = read(obj), write](Component &c) { write(c, value); });
    };
    QObject::connect(obj, signal, obj, push);
    push();
}

inline std::string utf8(const QString &text)
{
    return text.toStdString();
}

inline slint::SharedString str(const std::string &text)
{
    return slint::SharedString(std::string_view(text));
}

} // namespace bridge
