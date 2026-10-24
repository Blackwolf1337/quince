#include "ImageCache.h"
#include "app/ArtCache.h"
#include "ytm/Parser.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <cstring>

namespace {

// covers kept as images; the disk cache holds the rest
constexpr size_t kMaxImages = 400;

std::string keyOf(const std::string &url, int pixels)
{
    return url + '#' + std::to_string(pixels);
}

} // namespace

// Lives on the core thread: downloads, then decodes on the thread pool.
class ImageCache::Fetcher : public QObject
{
public:
    Fetcher(ImageCache *cache, QObject *parent)
        : QObject(parent)
        , m_cache(cache)
        , m_network(createArtNetwork(this))
    {
    }

    void fetch(const std::string &key, const QString &url, int pixels)
    {
        QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(ytm::artUrl(url, pixels))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, pixels] {
            reply->deleteLater();
            QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
            ImageCache *cache = m_cache;
            QThreadPool::globalInstance()->start([cache, key, pixels, data] {
                QImage image;
                if (!data.isEmpty())
                    image.loadFromData(data);
                if (!image.isNull() && (image.width() > pixels * 2 || image.height() > pixels * 2))
                    image = image.scaled(pixels, pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                image = image.convertToFormat(QImage::Format_RGBA8888);
                std::vector<unsigned char> rgba;
                for (int y = 0; y < image.height(); y++) {
                    const uchar *line = image.constScanLine(y);
                    rgba.insert(rgba.end(), line, line + size_t(image.width()) * 4);
                }
                const int w = image.width(), h = image.height();
                slint::invoke_from_event_loop([cache, key, w, h, rgba = std::move(rgba)]() mutable {
                    cache->arrived(key, w, h, std::move(rgba));
                });
            });
        });
    }

private:
    ImageCache *m_cache;
    QNetworkAccessManager *m_network;
};

ImageCache::ImageCache() = default;
ImageCache::~ImageCache() = default;

void ImageCache::startCore(QObject *coreParent)
{
    auto *fetcher = new Fetcher(this, coreParent);
    slint::invoke_from_event_loop([this, fetcher] {
        m_fetcher = fetcher;
        auto unsent = std::move(m_unsent);
        m_unsent.clear();
        for (const auto &[key, request] : unsent)
            send(key, request.first, request.second);
    });
}

void ImageCache::clear()
{
    m_images.clear();
    m_ages.clear();
    m_waiting.clear();
    m_unsent.clear();
}

void ImageCache::get(const std::string &url, int pixels, Done done)
{
    if (url.empty()) {
        done({});
        return;
    }
    const std::string key = keyOf(url, pixels);
    if (auto it = m_images.find(key); it != m_images.end()) {
        m_ages.splice(m_ages.begin(), m_ages, it->second.age);
        done(it->second.image);
        return;
    }
    auto &waiters = m_waiting[key];
    waiters.push_back(std::move(done));
    if (waiters.size() > 1)
        return;
    if (!m_fetcher) {
        m_unsent[key] = { url, pixels };
        return;
    }
    send(key, url, pixels);
}

void ImageCache::send(const std::string &key, const std::string &url, int pixels)
{
    Fetcher *fetcher = m_fetcher;
    QMetaObject::invokeMethod(fetcher, [fetcher, key, url = QString::fromStdString(url), pixels] {
        fetcher->fetch(key, url, pixels);
    }, Qt::QueuedConnection);
}

void ImageCache::arrived(const std::string &key, int width, int height, std::vector<unsigned char> rgba)
{
    slint::Image image;
    if (width > 0 && height > 0) {
        slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer { uint32_t(width), uint32_t(height) };
        std::memcpy(buffer.begin(), rgba.data(), rgba.size());
        image = slint::Image(buffer);
        m_ages.push_front(key);
        m_images[key] = { image, m_ages.begin() };
        while (m_images.size() > kMaxImages) {
            m_images.erase(m_ages.back());
            m_ages.pop_back();
        }
    }
    auto waiters = std::move(m_waiting[key]);
    m_waiting.erase(key);
    for (const Done &done : waiters)
        done(image);
}
