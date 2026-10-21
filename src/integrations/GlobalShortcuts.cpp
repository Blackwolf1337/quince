#include "integrations/GlobalShortcuts.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QRandomGenerator>

Q_LOGGING_CATEGORY(lcShortcuts, "quince.shortcuts")

namespace {

const QString kPortal = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

// Replies of a portal call come as a Response signal on a request object
// whose path follows from our bus name and a token we choose; the handler
// listens there before the call goes out.
class PortalResponse : public QObject
{
    Q_OBJECT

public:
    explicit PortalResponse(std::function<void(uint, const QVariantMap &)> done, QObject *parent)
        : QObject(parent)
        , m_done(std::move(done))
    {
    }

public slots:
    void onResponse(uint response, const QVariantMap &results)
    {
        m_done(response, results);
        deleteLater();
    }

private:
    std::function<void(uint, const QVariantMap &)> m_done;
};

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
}

void GlobalShortcuts::add(const QString &id, const QString &description, Handler handler)
{
    m_entries.append({id, description, std::move(handler)});
}

QString GlobalShortcuts::requestPath(const QString &token) const
{
    // ":1.42" -> "1_42"
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace(u'.', u'_');
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void GlobalShortcuts::call(const QString &method, const QVariantList &arguments, const QString &token,
                           std::function<void(uint, const QVariantMap &)> done)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *response = new PortalResponse(std::move(done), this);
    bus.connect(kPortal, requestPath(token), kRequest, QStringLiteral("Response"), response,
                SLOT(onResponse(uint, QVariantMap)));

    QDBusMessage message = QDBusMessage::createMethodCall(kPortal, kPortalPath, kInterface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, response, method] {
        watcher->deleteLater();
        if (watcher->isError()) {
            // no portal, or one without GlobalShortcuts: nothing to do
            qCDebug(lcShortcuts) << method << "failed:" << watcher->error().message();
            response->deleteLater();
        }
    });
}

void GlobalShortcuts::start()
{
    if (m_entries.isEmpty() || !QDBusConnection::sessionBus().isConnected())
        return;
    const QString token = QStringLiteral("quince%1").arg(QRandomGenerator::global()->generate());
    const QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("session_handle_token"), QStringLiteral("quince_session%1").arg(m_tokens++)},
    };
    call(QStringLiteral("CreateSession"), {options}, token, [this](uint response, const QVariantMap &results) {
        if (response != 0) {
            qCWarning(lcShortcuts) << "portal refused a shortcut session:" << response;
            return;
        }
        m_session = results.value(QStringLiteral("session_handle")).toString();
        QDBusConnection::sessionBus().connect(kPortal, kPortalPath, kInterface, QStringLiteral("Activated"), this,
                                              SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
        bind();
    });
}

void GlobalShortcuts::bind()
{
    QList<PortalShortcut> shortcuts;
    for (const Entry &e : std::as_const(m_entries))
        shortcuts.append({e.id, {{QStringLiteral("description"), e.description}}});

    const QString token = QStringLiteral("quince%1").arg(QRandomGenerator::global()->generate());
    call(QStringLiteral("BindShortcuts"),
         {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts), QString(),
          QVariantMap{{QStringLiteral("handle_token"), token}}},
         token, [](uint response, const QVariantMap &) {
             qCDebug(lcShortcuts) << "shortcuts bound, response" << response;
         });
}

void GlobalShortcuts::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &)
{
    if (session.path() != m_session)
        return;
    for (const Entry &e : std::as_const(m_entries)) {
        if (e.id == id) {
            e.handler();
            return;
        }
    }
}

#include "GlobalShortcuts.moc"
