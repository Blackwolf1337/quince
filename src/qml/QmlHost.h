#pragma once

#include "app/UiHost.h"

#include <QPointer>
#include <QWindow>

class Tray;

// UiHost for the QML window: the window itself, the system clipboard and
// QDesktopServices; notifications without D-Bus go through the tray icon.
class QmlHost : public UiHost
{
public:
    void setWindow(QWindow *window) { m_window = window; }
    void setTray(Tray *tray) { m_tray = tray; }

    bool windowInFront() const override;
    void toggleWindow() override;
    void raiseWindow() override;
    void setClipboard(const QString &text) override;
    void openUrl(const QUrl &url) override;
    void showNotification(const QString &title, const QString &body, const QImage &cover) override;

private:
    QPointer<QWindow> m_window;
    Tray *m_tray = nullptr;
};
