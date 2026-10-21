#include "app/ArtNetwork.h"

#include <QAtomicInt>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

constexpr qint64 kCacheBytes = 256ll * 1024 * 1024;

class ArtNetworkAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        QNetworkRequest cached(request);
        cached.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, cached, data);
    }
};

} // namespace

// QML creates one manager per thread that loads from the network (normally
// only the image reader thread). Two QNetworkDiskCache objects must not share
// a directory, so any further one gets its own.
QNetworkAccessManager *ArtNetworkFactory::create(QObject *parent)
{
    static QAtomicInt instances;
    const int n = instances.fetchAndAddRelaxed(1);

    auto *manager = new ArtNetworkAccessManager(parent);
    auto *cache = new QNetworkDiskCache(manager);
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/art");
    if (n > 0)
        dir += QString::number(n);
    cache->setCacheDirectory(dir);
    cache->setMaximumCacheSize(kCacheBytes);
    manager->setCache(cache);
    return manager;
}
