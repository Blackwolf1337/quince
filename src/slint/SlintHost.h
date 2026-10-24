#pragma once

#include "Bridge.h"
#include "app/UiHost.h"

#include <atomic>

// UiHost for the Slint window. The core calls it on its own thread; the
// window calls go to the Slint thread, windowInFront() answers from the state
// the Slint thread last reported.
class SlintHost : public UiHost
{
public:
    explicit SlintHost(bridge::Window window);

    // Slint thread: the window's state, for windowInFront()
    void setWindowState(bool visible, bool minimized);

    bool windowInFront() const override { return m_inFront; }
    void toggleWindow() override;
    void raiseWindow() override;
    void setClipboard(const QString &text) override;
    void openUrl(const QUrl &url) override;
    // Slint's tray has no bubbles: Windows and macOS notifications come with
    // their own code (phase 6); Linux sends them over D-Bus in the core
    void showNotification(const QString &, const QString &, const QImage &) override {}

private:
    bridge::Window m_window;
    // visible and not minimized; Slint does not tell whether it has the focus
    std::atomic<bool> m_inFront = false;
};
