#include "qml/QmlHost.h"
#include "qml/Tray.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>

bool QmlHost::windowInFront() const
{
    return m_window && m_window->isVisible() && m_window->isActive();
}

void QmlHost::toggleWindow()
{
    if (!m_window)
        return;
    if (windowInFront()) {
        m_window->hide();
        return;
    }
    raiseWindow();
}

void QmlHost::raiseWindow()
{
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void QmlHost::setClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void QmlHost::openUrl(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void QmlHost::showNotification(const QString &title, const QString &body, const QImage &cover)
{
    if (m_tray)
        m_tray->showMessage(title, body, cover);
}
