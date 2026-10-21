#pragma once

#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

// One entry of the portal's a(sa{sv}) shortcut list.
struct PortalShortcut
{
    QString id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PortalShortcut)
QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut);

// System-wide shortcuts through org.freedesktop.portal.GlobalShortcuts. On
// Hyprland they become targets of the "global" dispatcher (quince:<id>), on
// KDE and GNOME the desktop asks which keys to use. Linux only.
class GlobalShortcuts : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void()>;

    explicit GlobalShortcuts(QObject *parent = nullptr);

    // id, what the desktop shows for it, what it does
    void add(const QString &id, const QString &description, Handler handler);
    // asks the portal for a session and binds everything added before
    void start();

private slots:
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);

private:
    QString requestPath(const QString &token) const;
    void call(const QString &method, const QVariantList &arguments, const QString &token,
              std::function<void(uint response, const QVariantMap &results)> done);
    void bind();

    struct Entry
    {
        QString id;
        QString description;
        Handler handler;
    };
    QList<Entry> m_entries;
    QString m_session;
    int m_tokens = 0;
};
