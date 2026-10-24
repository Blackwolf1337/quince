#include "ImageCache.h"
#include "app/ArtCache.h"
#include "ytm/Parser.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace {

// covers kept as images; the disk cache holds the rest
constexpr size_t kMaxImages = 400;

// blurred covers: downloaded this large, blurred at kBlurSize (Slint scales
// them up smoothly), with three box blurs, nearly a Gaussian
constexpr int kBlurSource = 120;
constexpr int kBlurSize = 64;
constexpr int kBlurRadius = 5;
constexpr int kBlurPasses = 3;
// the look of the QML backdrop's MultiEffect: a quarter more saturation, darker
constexpr float kBlurSaturation = 1.25f;
constexpr float kBlurBrightness = -0.35f;

std::string keyOf(const std::string &url, int pixels)
{
    return url + '#' + std::to_string(pixels);
}

// one box blur of the RGB planes along x (step 1) or y (step w), edges clamped
void boxBlur(const std::vector<float> &src, std::vector<float> &dst, int w, int h, bool alongX)
{
    const int len = alongX ? w : h, lines = alongX ? h : w;
    const int step = alongX ? 3 : w * 3;
    for (int line = 0; line < lines; line++) {
        const int base = alongX ? line * w * 3 : line * 3;
        for (int c = 0; c < 3; c++) {
            const auto at = [&](int i) { return src[size_t(base + std::clamp(i, 0, len - 1) * step + c)]; };
            float sum = 0;
            for (int i = -kBlurRadius; i <= kBlurRadius; i++)
                sum += at(i);
            for (int i = 0; i < len; i++) {
                dst[size_t(base + i * step + c)] = sum / float(2 * kBlurRadius + 1);
                sum += at(i + kBlurRadius + 1) - at(i - kBlurRadius);
            }
        }
    }
}

QImage blurredCover(const QImage &cover)
{
    QImage image = cover.scaled(kBlurSize, kBlurSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGBA8888);
    const int w = image.width(), h = image.height();
    std::vector<float> plane(size_t(w) * size_t(h) * 3), tmp(plane.size());
    for (int y = 0; y < h; y++) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
                plane[size_t((y * w + x) * 3 + c)] = line[x * 4 + c] / 255.0f;
    }
    for (int i = 0; i < kBlurPasses; i++) {
        boxBlur(plane, tmp, w, h, true);
        boxBlur(tmp, plane, w, h, false);
    }
    for (int y = 0; y < h; y++) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < w; x++) {
            const float *p = &plane[size_t((y * w + x) * 3)];
            const float gray = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            for (int c = 0; c < 3; c++) {
                const float v = gray + (p[c] - gray) * kBlurSaturation + kBlurBrightness;
                line[x * 4 + c] = uchar(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            line[x * 4 + 3] = 255;
        }
    }
    return image;
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

    void fetch(const std::string &key, const QString &url, int pixels, bool blurred)
    {
        QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(ytm::artUrl(url, pixels))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, pixels, blurred] {
            reply->deleteLater();
            QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
            ImageCache *cache = m_cache;
            QThreadPool::globalInstance()->start([cache, key, pixels, blurred, data] {
                QImage image;
                if (!data.isEmpty())
                    image.loadFromData(data);
                if (!image.isNull() && blurred)
                    image = blurredCover(image);
                else if (!image.isNull() && (image.width() > pixels * 2 || image.height() > pixels * 2))
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
        for (const auto &[key, r] : unsent)
            send(key, r);
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
    request(keyOf(url, pixels), { url, pixels, false }, std::move(done));
}

void ImageCache::getBlurred(const std::string &url, Done done)
{
    request(url + "#blurred", { url, kBlurSource, true }, std::move(done));
}

void ImageCache::request(const std::string &key, const Request &r, Done done)
{
    if (r.url.empty()) {
        done({});
        return;
    }
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
        m_unsent[key] = r;
        return;
    }
    send(key, r);
}

void ImageCache::send(const std::string &key, const Request &r)
{
    Fetcher *fetcher = m_fetcher;
    QMetaObject::invokeMethod(fetcher, [fetcher, key, url = QString::fromStdString(r.url), pixels = r.pixels,
                                        blurred = r.blurred] {
        fetcher->fetch(key, url, pixels, blurred);
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
