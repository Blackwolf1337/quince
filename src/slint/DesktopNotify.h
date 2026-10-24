#pragma once

#include <QImage>
#include <QString>

// A track notification where the core has no D-Bus: a toast on Windows (the
// tray bubble of Shell_NotifyIcon, which Windows 10 and 11 show as one), the
// notification center on macOS (only inside an app bundle). Core thread.
void desktopNotify(const QString &title, const QString &body, const QImage &cover);
