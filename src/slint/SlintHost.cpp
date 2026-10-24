#include "SlintHost.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <QProcess>
#endif

SlintHost::SlintHost(bridge::Window window)
    : m_window(std::move(window))
{
}

void SlintHost::setWindowState(bool visible, bool minimized)
{
    m_inFront = visible && !minimized;
}

void SlintHost::toggleWindow()
{
    bridge::toUi(m_window, [this](MainWindow &w) {
        if (w.window().is_visible() && !w.window().is_minimized()) {
            w.hide();
            setWindowState(false, false);
            return;
        }
        w.show();
        w.window().set_minimized(false);
        setWindowState(true, false);
    });
}

void SlintHost::raiseWindow()
{
    bridge::toUi(m_window, [this](MainWindow &w) {
        w.show();
        w.window().set_minimized(false);
        setWindowState(true, false);
    });
}

void SlintHost::setClipboard(const QString &text)
{
    bridge::toUi(m_window, [text = bridge::utf8(text)](MainWindow &w) { w.invoke_copy_text(bridge::str(text)); });
}

void SlintHost::openUrl(const QUrl &url)
{
#if defined(_WIN32)
    const std::wstring target = (url.isLocalFile() ? url.toLocalFile() : url.toString()).toStdWString();
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    QProcess::startDetached(QStringLiteral("open"), {url.toString()});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()});
#endif
}
