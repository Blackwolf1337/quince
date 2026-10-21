#include "ytm/BrowserCookies.h"
#include "ytm/NetscapeCookies.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPasswordDigestor>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#if defined(Q_OS_LINUX) && defined(QUINCE_DBUS)
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#endif

#if defined(Q_OS_LINUX)
#include <openssl/evp.h>
#elif defined(Q_OS_MACOS)
#include <CommonCrypto/CommonCryptor.h>
#include <QProcess>
#endif

Q_LOGGING_CATEGORY(lcBrowser, "quince.browser")

namespace {

enum class Family { Firefox, Chromium };

struct Kind
{
    const char *id;
    const char *name;
    Family family;
    // Chromium: "<keyring> Safe Storage" names the key in the OS keyring
    const char *keyring;
};

#if defined(Q_OS_MACOS)
constexpr const char *kVivaldiKeyring = "Vivaldi";
constexpr const char *kEdgeKeyring = "Microsoft Edge";
#else
constexpr const char *kVivaldiKeyring = "Chrome";
constexpr const char *kEdgeKeyring = "Chromium";
#endif

// as yt-dlp names them (cookies.py, _get_chromium_based_browser_settings)
const Kind kKinds[] = {
    {"firefox", "Firefox", Family::Firefox, nullptr},
    {"zen", "Zen", Family::Firefox, nullptr},
    {"librewolf", "LibreWolf", Family::Firefox, nullptr},
    {"floorp", "Floorp", Family::Firefox, nullptr},
    {"waterfox", "Waterfox", Family::Firefox, nullptr},
#if !defined(Q_OS_WIN)
    {"chrome", "Chrome", Family::Chromium, "Chrome"},
    {"chromium", "Chromium", Family::Chromium, "Chromium"},
    {"brave", "Brave", Family::Chromium, "Brave"},
    {"vivaldi", "Vivaldi", Family::Chromium, kVivaldiKeyring},
    {"edge", "Edge", Family::Chromium, kEdgeKeyring},
#endif
};

const Kind *kindOf(const QString &id)
{
    for (const Kind &kind : kKinds) {
        if (id == QLatin1StringView(kind.id))
            return &kind;
    }
    return nullptr;
}

QString home(const QString &path)
{
    return QDir::homePath() + u'/' + path;
}

QString configHome(const QString &path)
{
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    return config + u'/' + path;
}

// folders holding the browser's profiles
QStringList roots(const Kind &kind)
{
    const QString id = QString::fromLatin1(kind.id);
#if defined(Q_OS_WIN)
    const QString appData = qEnvironmentVariable("APPDATA");
    const QHash<QString, QStringList> dirs{
        {QStringLiteral("firefox"), {appData + QStringLiteral("/Mozilla/Firefox/Profiles"),
                                     qEnvironmentVariable("LOCALAPPDATA")
                                         + QStringLiteral("/Packages/Mozilla.Firefox_n80bbvh6b1yt2/LocalCache/Roaming/Mozilla/Firefox/Profiles")}},
        {QStringLiteral("zen"), {appData + QStringLiteral("/zen/Profiles")}},
        {QStringLiteral("librewolf"), {appData + QStringLiteral("/librewolf/Profiles")}},
        {QStringLiteral("floorp"), {appData + QStringLiteral("/Floorp/Profiles")}},
        {QStringLiteral("waterfox"), {appData + QStringLiteral("/Waterfox/Profiles")}},
    };
#elif defined(Q_OS_MACOS)
    const QString support = home(QStringLiteral("Library/Application Support/"));
    const QHash<QString, QStringList> dirs{
        {QStringLiteral("firefox"), {support + QStringLiteral("Firefox/Profiles")}},
        {QStringLiteral("zen"), {support + QStringLiteral("zen/Profiles")}},
        {QStringLiteral("librewolf"), {support + QStringLiteral("librewolf/Profiles")}},
        {QStringLiteral("floorp"), {support + QStringLiteral("Floorp/Profiles")}},
        {QStringLiteral("waterfox"), {support + QStringLiteral("Waterfox/Profiles")}},
        {QStringLiteral("chrome"), {support + QStringLiteral("Google/Chrome")}},
        {QStringLiteral("chromium"), {support + QStringLiteral("Chromium")}},
        {QStringLiteral("brave"), {support + QStringLiteral("BraveSoftware/Brave-Browser")}},
        {QStringLiteral("vivaldi"), {support + QStringLiteral("Vivaldi")}},
        {QStringLiteral("edge"), {support + QStringLiteral("Microsoft Edge")}},
    };
#else
    // native, then Flatpak and Snap installs
    const QHash<QString, QStringList> dirs{
        {QStringLiteral("firefox"), {configHome(QStringLiteral("mozilla/firefox")), home(QStringLiteral(".mozilla/firefox")),
                                     home(QStringLiteral(".var/app/org.mozilla.firefox/config/mozilla/firefox")),
                                     home(QStringLiteral(".var/app/org.mozilla.firefox/.mozilla/firefox")),
                                     home(QStringLiteral("snap/firefox/common/.mozilla/firefox"))}},
        {QStringLiteral("zen"), {home(QStringLiteral(".zen")), configHome(QStringLiteral("zen")),
                                 home(QStringLiteral(".var/app/app.zen_browser.zen/.zen"))}},
        {QStringLiteral("librewolf"), {home(QStringLiteral(".librewolf")), configHome(QStringLiteral("librewolf/librewolf")),
                                       home(QStringLiteral(".var/app/io.gitlab.librewolf-community/.librewolf"))}},
        {QStringLiteral("floorp"), {home(QStringLiteral(".floorp")), home(QStringLiteral(".var/app/one.ablaze.floorp/.floorp"))}},
        {QStringLiteral("waterfox"), {home(QStringLiteral(".waterfox")), home(QStringLiteral(".var/app/net.waterfox.waterfox/.waterfox"))}},
        {QStringLiteral("chrome"), {configHome(QStringLiteral("google-chrome")),
                                    home(QStringLiteral(".var/app/com.google.Chrome/config/google-chrome"))}},
        {QStringLiteral("chromium"), {configHome(QStringLiteral("chromium")),
                                      home(QStringLiteral(".var/app/org.chromium.Chromium/config/chromium")),
                                      home(QStringLiteral("snap/chromium/common/chromium"))}},
        {QStringLiteral("brave"), {configHome(QStringLiteral("BraveSoftware/Brave-Browser")),
                                   home(QStringLiteral(".var/app/com.brave.Browser/config/BraveSoftware/Brave-Browser"))}},
        {QStringLiteral("vivaldi"), {configHome(QStringLiteral("vivaldi")),
                                     home(QStringLiteral(".var/app/com.vivaldi.Vivaldi/config/vivaldi"))}},
        {QStringLiteral("edge"), {configHome(QStringLiteral("microsoft-edge")),
                                  home(QStringLiteral(".var/app/com.microsoft.Edge/config/microsoft-edge"))}},
    };
#endif
    return dirs.value(id);
}

// cookie databases of every profile, the newest first
QStringList databases(const Kind &kind)
{
    QList<QFileInfo> found;
    for (const QString &root : roots(kind)) {
        const QDir dir(root);
        if (!dir.exists())
            continue;
        const QFileInfoList profiles = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &profile : profiles) {
            const QStringList names = kind.family == Family::Firefox
                ? QStringList{QStringLiteral("cookies.sqlite")}
                : QStringList{QStringLiteral("Network/Cookies"), QStringLiteral("Cookies")};
            for (const QString &name : names) {
                const QFileInfo db(profile.filePath() + u'/' + name);
                if (db.isFile()) {
                    found.append(db);
                    break;
                }
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const QFileInfo &a, const QFileInfo &b) {
        // a WAL file is written before the database itself
        const auto touched = [](const QFileInfo &db) {
            const QFileInfo wal(db.filePath() + QStringLiteral("-wal"));
            return qMax(db.lastModified(), wal.exists() ? wal.lastModified() : QDateTime());
        };
        return touched(a) > touched(b);
    });
    QStringList out;
    for (const QFileInfo &db : std::as_const(found))
        out.append(db.filePath());
    return out;
}

// The browser keeps its database open and locked; a copy (with its WAL file,
// which holds what was written last) can be read.
class DatabaseCopy
{
public:
    explicit DatabaseCopy(const QString &path)
        : m_connection(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        if (!m_dir.isValid())
            return;
        QFile::setPermissions(m_dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString copy = m_dir.filePath(QStringLiteral("cookies.db"));
        if (!QFile::copy(path, copy))
            return;
        QFile::copy(path + QStringLiteral("-wal"), copy + QStringLiteral("-wal"));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        db.setDatabaseName(copy);
        m_open = db.open();
        if (!m_open)
            m_error = db.lastError().text();
    }
    ~DatabaseCopy()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connection, false);
            if (db.isValid())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
    DatabaseCopy(const DatabaseCopy &) = delete;
    DatabaseCopy &operator=(const DatabaseCopy &) = delete;

    bool open() const { return m_open; }
    QString error() const { return m_error.isEmpty() ? QStringLiteral("could not copy the database") : m_error; }
    QSqlDatabase database() const { return QSqlDatabase::database(m_connection, false); }

private:
    QTemporaryDir m_dir;
    QString m_connection;
    bool m_open = false;
    QString m_error;
};

// only rows of the sites Quince keeps; the rest is not even decrypted
const QString kAccountHosts = QStringLiteral(
    "(%1 LIKE '%youtube.com' OR %1 LIKE '%google.com')");

QList<QNetworkCookie> readFirefox(const QString &path, QString *error)
{
    DatabaseCopy copy(path);
    if (!copy.open()) {
        *error = copy.error();
        return {};
    }
    QSqlQuery version(copy.database());
    version.exec(QStringLiteral("PRAGMA user_version"));
    const int schema = version.next() ? version.value(0).toInt() : 0;

    QSqlQuery query(copy.database());
    if (!query.exec(QStringLiteral("SELECT host, name, value, path, expiry, isSecure, isHttpOnly FROM moz_cookies WHERE ")
                    + kAccountHosts.arg(QStringLiteral("host")))) {
        *error = query.lastError().text();
        return {};
    }
    QList<QNetworkCookie> out;
    while (query.next()) {
        QNetworkCookie cookie(query.value(1).toByteArray(), query.value(2).toByteArray());
        cookie.setDomain(query.value(0).toString());
        cookie.setPath(query.value(3).toString());
        qint64 expiry = query.value(4).toLongLong();
        // Firefox 142 (schema 16) went to milliseconds
        if (schema >= 16)
            expiry /= 1000;
        if (expiry > 0)
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expiry, QTimeZone::UTC));
        cookie.setSecure(query.value(5).toBool());
        cookie.setHttpOnly(query.value(6).toBool());
        out.append(cookie);
    }
    return out;
}

#if !defined(Q_OS_WIN)

QByteArray deriveKey(const QByteArray &password, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, password, QByteArrayLiteral("saltysalt"),
                                              iterations, 16);
}

// AES-128-CBC with Chromium's IV of 16 spaces; empty when the key does not fit
QByteArray decryptCbc(const QByteArray &ciphertext, const QByteArray &key)
{
    static const QByteArray iv(16, ' ');
    QByteArray plain(ciphertext.size() + 16, Qt::Uninitialized);
#if defined(Q_OS_LINUX)
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int length = 0;
    int last = 0;
    const bool ok = ctx
        && EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plain.data()), &length,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()), int(ciphertext.size())) == 1
        && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(plain.data()) + length, &last) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    plain.resize(length + last);
#else
    size_t moved = 0;
    if (CCCrypt(kCCDecrypt, kCCAlgorithmAES128, kCCOptionPKCS7Padding, key.constData(), key.size(), iv.constData(),
                ciphertext.constData(), size_t(ciphertext.size()), plain.data(), size_t(plain.size()), &moved)
        != kCCSuccess)
        return {};
    plain.resize(qsizetype(moved));
#endif
    return plain;
}

#if defined(Q_OS_LINUX) && defined(QUINCE_DBUS)

// org.freedesktop.DBus.Properties.Get, without relying on introspection
QVariant dbusProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("Get"));
    call << interface << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().at(0).value<QDBusVariant>().variant();
}

// The Secret Service (GNOME Keyring, KeePassXC, KDE's ksecretd): the item
// labelled "<keyring> Safe Storage" in the default collection.
QByteArray secretServicePassword(const QString &label)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.freedesktop.secrets");
    QDBusInterface secrets(service, QStringLiteral("/org/freedesktop/secrets"),
                           QStringLiteral("org.freedesktop.Secret.Service"), bus);
    if (!secrets.isValid())
        return {};
    const QDBusMessage session = secrets.call(QStringLiteral("OpenSession"), QStringLiteral("plain"),
                                              QVariant::fromValue(QDBusVariant(QString())));
    if (session.type() != QDBusMessage::ReplyMessage || session.arguments().size() < 2)
        return {};
    const QDBusObjectPath sessionPath = session.arguments().at(1).value<QDBusObjectPath>();

    const QDBusReply<QDBusObjectPath> alias = secrets.call(QStringLiteral("ReadAlias"), QStringLiteral("default"));
    QByteArray password;
    if (alias.isValid() && alias.value().path() != u"/") {
        const QVariant itemList = dbusProperty(service, alias.value().path(),
                                               QStringLiteral("org.freedesktop.Secret.Collection"), QStringLiteral("Items"));
        const QList<QDBusObjectPath> items = qdbus_cast<QList<QDBusObjectPath>>(itemList);
        for (const QDBusObjectPath &item : items) {
            const QString itemLabel = dbusProperty(service, item.path(), QStringLiteral("org.freedesktop.Secret.Item"),
                                                   QStringLiteral("Label")).toString();
            if (itemLabel != label)
                continue;
            QDBusMessage call = QDBusMessage::createMethodCall(service, item.path(), QStringLiteral("org.freedesktop.Secret.Item"),
                                                               QStringLiteral("GetSecret"));
            call << QVariant::fromValue(sessionPath);
            const QDBusMessage secret = bus.call(call);
            if (secret.type() != QDBusMessage::ReplyMessage || secret.arguments().isEmpty()) {
                qCWarning(lcBrowser) << "keyring item not readable (locked?)";
                break;
            }
            // (session, parameters, value, content type)
            const QDBusArgument argument = secret.arguments().at(0).value<QDBusArgument>();
            QDBusObjectPath unused;
            QByteArray parameters;
            QString contentType;
            argument.beginStructure();
            argument >> unused >> parameters >> password >> contentType;
            argument.endStructure();
            break;
        }
    }
    QDBusInterface(service, sessionPath.path(), QStringLiteral("org.freedesktop.Secret.Session"), bus)
        .call(QStringLiteral("Close"));
    return password;
}

// KWallet: folder "<keyring> Keys", entry "<keyring> Safe Storage"
QByteArray kwalletPassword(const QString &keyring)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString appId = QStringLiteral("quince");
    for (const QString &daemon : {QStringLiteral("kwalletd6"), QStringLiteral("kwalletd5")}) {
        QDBusInterface wallet(QStringLiteral("org.kde.") + daemon, QStringLiteral("/modules/") + daemon,
                              QStringLiteral("org.kde.KWallet"), bus);
        if (!wallet.isValid())
            continue;
        const QDBusReply<QString> name = wallet.call(QStringLiteral("networkWallet"));
        if (!name.isValid())
            continue;
        const QDBusReply<int> handle = wallet.call(QStringLiteral("open"), name.value(), qlonglong(0), appId);
        if (!handle.isValid() || handle.value() < 0)
            continue;
        const QDBusReply<QString> password = wallet.call(QStringLiteral("readPassword"), handle.value(),
                                                         keyring + QStringLiteral(" Keys"),
                                                         keyring + QStringLiteral(" Safe Storage"), appId);
        return password.isValid() ? password.value().toUtf8() : QByteArray();
    }
    return {};
}

#endif

// The key of v11 cookies (Linux) or of all cookies (macOS); empty if none.
QByteArray osKey(const Kind &kind)
{
    const QString keyring = QString::fromLatin1(kind.keyring);
#if defined(Q_OS_LINUX) && defined(QUINCE_DBUS)
    QByteArray password = secretServicePassword(keyring + QStringLiteral(" Safe Storage"));
    if (password.isEmpty())
        password = kwalletPassword(keyring);
    return password.isEmpty() ? QByteArray() : deriveKey(password, 1);
#elif defined(Q_OS_MACOS)
    // never built or run by the author; the same as yt-dlp's macOS path
    QProcess security;
    security.start(QStringLiteral("security"), {QStringLiteral("find-generic-password"), QStringLiteral("-w"),
                                                 QStringLiteral("-s"), keyring + QStringLiteral(" Safe Storage")});
    if (!security.waitForFinished(30000) || security.exitCode() != 0)
        return {};
    const QByteArray password = security.readAllStandardOutput().trimmed();
    return password.isEmpty() ? QByteArray() : deriveKey(password, 1003);
#else
    Q_UNUSED(keyring);
    return {};
#endif
}

QList<QNetworkCookie> readChromium(const QString &path, const Kind &kind, QString *error)
{
    DatabaseCopy copy(path);
    if (!copy.open()) {
        *error = copy.error();
        return {};
    }
    QSqlQuery meta(copy.database());
    meta.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'version'"));
    // from version 24 on, the plaintext starts with a SHA-256 of the host
    const bool hashPrefix = meta.next() && meta.value(0).toInt() >= 24;

    QSqlQuery query(copy.database());
    if (!query.exec(QStringLiteral("SELECT * FROM cookies WHERE ") + kAccountHosts.arg(QStringLiteral("host_key")))) {
        *error = query.lastError().text();
        return {};
    }
    const QSqlRecord record = query.record();
    const int hostField = record.indexOf(QStringLiteral("host_key"));
    const int nameField = record.indexOf(QStringLiteral("name"));
    const int valueField = record.indexOf(QStringLiteral("value"));
    const int encryptedField = record.indexOf(QStringLiteral("encrypted_value"));
    const int pathField = record.indexOf(QStringLiteral("path"));
    const int expiresField = record.indexOf(QStringLiteral("expires_utc"));
    int secureField = record.indexOf(QStringLiteral("is_secure"));
    if (secureField < 0)
        secureField = record.indexOf(QStringLiteral("secure"));
    int httpOnlyField = record.indexOf(QStringLiteral("is_httponly"));
    if (httpOnlyField < 0)
        httpOnlyField = record.indexOf(QStringLiteral("httponly"));

#if defined(Q_OS_LINUX)
    const QByteArray v10Key = deriveKey(QByteArrayLiteral("peanuts"), 1);
    const QByteArray emptyKey = deriveKey(QByteArray(), 1);
#endif
    QByteArray v11Key;
    bool askedKeyring = false;
    int undecrypted = 0;

    QList<QNetworkCookie> out;
    while (query.next()) {
        QByteArray value = query.value(valueField).toByteArray();
        const QByteArray encrypted = query.value(encryptedField).toByteArray();
        if (value.isEmpty() && !encrypted.isEmpty()) {
            const QByteArray version = encrypted.left(3);
            const QByteArray ciphertext = encrypted.mid(3);
            QList<QByteArray> keys;
#if defined(Q_OS_LINUX)
            if (version == "v10") {
                keys = {v10Key, emptyKey};
            } else if (version == "v11") {
                if (!askedKeyring) {
                    askedKeyring = true;
                    v11Key = osKey(kind);
                }
                keys = {v11Key, emptyKey};
            }
#else
            if (version == "v10") {
                if (!askedKeyring) {
                    askedKeyring = true;
                    v11Key = osKey(kind);
                }
                keys = {v11Key};
            }
#endif
            value.clear();
            for (const QByteArray &key : std::as_const(keys)) {
                if (key.isEmpty())
                    continue;
                QByteArray plain = decryptCbc(ciphertext, key);
                if (hashPrefix)
                    plain.remove(0, 32);
                // a wrong key leaves bytes that are no text
                QStringDecoder utf8(QStringDecoder::Utf8);
                const QString text = utf8(plain);
                if (!plain.isEmpty() && !utf8.hasError()) {
                    value = text.toUtf8();
                    break;
                }
            }
            if (value.isEmpty()) {
                ++undecrypted;
                continue;
            }
        }
        QNetworkCookie cookie(query.value(nameField).toByteArray(), value);
        cookie.setDomain(query.value(hostField).toString());
        cookie.setPath(query.value(pathField).toString());
        // microseconds since 1601; 0 for session cookies
        const qint64 expires = query.value(expiresField).toLongLong();
        if (expires > 0)
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expires / 1000000 - 11644473600LL, QTimeZone::UTC));
        if (secureField >= 0)
            cookie.setSecure(query.value(secureField).toBool());
        if (httpOnlyField >= 0)
            cookie.setHttpOnly(query.value(httpOnlyField).toBool());
        out.append(cookie);
    }
    if (undecrypted > 0) {
        qCWarning(lcBrowser) << undecrypted << "cookies not decrypted, keyring key" << (v11Key.isEmpty() ? "missing" : "wrong");
        if (out.isEmpty())
            *error = QCoreApplication::translate("BrowserCookies", "The browser's cookies are locked with a key from the "
                                                                   "system keyring, which could not be read.");
    }
    return out;
}

#endif

} // namespace

namespace BrowserCookies {

QList<Browser> installed()
{
    QList<Browser> out;
    for (const Kind &kind : kKinds) {
        if (!databases(kind).isEmpty())
            out.append({QString::fromLatin1(kind.id), QString::fromLatin1(kind.name)});
    }
    return out;
}

QList<QNetworkCookie> read(const QString &browserId, QString *error)
{
    const Kind *kind = kindOf(browserId);
    if (!kind) {
        *error = QCoreApplication::translate("BrowserCookies", "This browser is not supported.");
        return {};
    }
    const QStringList found = databases(*kind);
    if (found.isEmpty()) {
        *error = QCoreApplication::translate("BrowserCookies", "No profile of %1 was found.")
                     .arg(QString::fromLatin1(kind->name));
        return {};
    }
    // the newest profile that holds a session
    QString lastError;
    for (const QString &path : found) {
        QString readError;
        QList<QNetworkCookie> cookies;
        if (kind->family == Family::Firefox) {
            cookies = readFirefox(path, &readError);
        } else {
#if !defined(Q_OS_WIN)
            cookies = readChromium(path, *kind, &readError);
#endif
        }
        cookies = NetscapeCookies::accountCookies(cookies);
        qCDebug(lcBrowser) << kind->id << QFileInfo(path).dir().dirName() << cookies.size() << "cookies" << readError;
        if (!NetscapeCookies::sapisid(cookies).isEmpty())
            return cookies;
        if (!readError.isEmpty())
            lastError = readError;
    }
    *error = !lastError.isEmpty()
        ? lastError
        : QCoreApplication::translate("BrowserCookies", "%1 is not signed in to YouTube.")
              .arg(QString::fromLatin1(kind->name));
    return {};
}

} // namespace BrowserCookies
