#include "app/ArtCache.h"

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

QNetworkAccessManager *createArtNetwork(QObject *parent, int n)
{
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
