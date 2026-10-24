#pragma once

#include <QImage>
#include <QString>
#include <QUrl>

// What the core needs from the user interface it runs under (the Slint
// one). The core calls it on its own thread; a frontend whose window lives on
// another thread forwards the calls and answers windowInFront() from a cached
// state.
class UiHost
{
public:
    virtual ~UiHost() = default;

    // the main window is visible and has the focus (the user sees changes)
    virtual bool windowInFront() const = 0;
    // hides the window when it is in front, else shows and raises it
    virtual void toggleWindow() = 0;
    virtual void raiseWindow() = 0;

    virtual void setClipboard(const QString &text) = 0;
    // a folder or web page in the system's program for it
    virtual void openUrl(const QUrl &url) = 0;
    // a track notification where the core has no way of its own (no D-Bus)
    virtual void showNotification(const QString &title, const QString &body, const QImage &cover) = 0;
};
