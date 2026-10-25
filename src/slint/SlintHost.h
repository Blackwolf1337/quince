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

    // Slint thread: the window's state, for windowInFront(), and its native
    // handle, for nativeWindow()
    void setWindowState(bool visible, bool minimized);
    void setNativeWindow(void *handle) { m_native = handle; }

    bool windowInFront() const override { return m_inFront; }
    void toggleWindow() override;
    void raiseWindow() override;
    void setClipboard(const QString &text) override;
    void openUrl(const QUrl &url) override;
    // Windows and macOS (DesktopNotify); Linux sends them over D-Bus in the core
    void showNotification(const QString &title, const QString &body, const QImage &cover) override;
    void *nativeWindow() const override { return m_native; }

private:
    bridge::Window m_window;
    // visible and not minimized; Slint does not tell whether it has the focus
    std::atomic<bool> m_inFront = false;
    std::atomic<void *> m_native = nullptr;
};
