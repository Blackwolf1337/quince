#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "player/Playback.h"
#include "ytm/Parser.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <algorithm>
#include <array>

namespace {

// cover size fetched for the analysis
constexpr int kCoverPixels = 120;
// the cover is scaled to this many pixels square before counting
constexpr int kSample = 24;
constexpr int kHueBuckets = 12;
constexpr int kFadeMs = 400;

QColor lerp(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

} // namespace

ColorTheme::ColorTheme(Playback *playback, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
    m_current = m_from = m_to = defaults();
    m_fade.setDuration(kFadeMs);
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &t) {
        m_current = mix(m_from, m_to, t.toDouble());
        emit colorsChanged();
    });

    connect(m_playback, &Playback::trackChanged, this, &ColorTheme::onTrackChanged);
    connect(m_settings, &Settings::changed, this, &ColorTheme::apply);
}

ColorTheme::Palette ColorTheme::defaults()
{
    return {QColor(0xff, 0xc1, 0x07), QColor(0x14, 0x14, 0x17), QColor(0x1d, 0x1d, 0x21), QColor(0x26, 0x26, 0x2b)};
}

// Accent bright enough for text on the dark background, backgrounds a very
// dark shade of the same hue.
ColorTheme::Palette ColorTheme::fromColor(const QColor &base)
{
    const float hue = base.hslHueF() < 0 ? 0.0f : base.hslHueF();
    const float saturation = base.hslSaturationF();
    const float accentSat = qBound(0.45f, saturation, 0.95f);
    const float accentLight = qBound(0.58f, base.lightnessF(), 0.75f);
    const float tint = qMin(saturation, 0.45f);
    return {
        QColor::fromHslF(hue, accentSat, accentLight),
        QColor::fromHslF(hue, tint, 0.075f),
        QColor::fromHslF(hue, tint, 0.105f),
        QColor::fromHslF(hue, tint * 0.9f, 0.145f),
    };
}

ColorTheme::Palette ColorTheme::mix(const Palette &a, const Palette &b, double t)
{
    return {lerp(a.accent, b.accent, t), lerp(a.background, b.background, t), lerp(a.surface, b.surface, t),
            lerp(a.surfaceHigh, b.surfaceHigh, t)};
}

QColor ColorTheme::dominantColor(const QImage &image)
{
    if (image.isNull())
        return {};
    // video thumbnails carry black bars; the middle square is the picture
    const int side = qMin(image.width(), image.height());
    const QImage square = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    const QImage small = square.scaled(kSample, kSample, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);

    struct Bucket
    {
        double weight = 0;
        double r = 0, g = 0, b = 0;
    };
    std::array<Bucket, kHueBuckets> buckets{};
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = QColor::fromRgb(small.pixel(x, y));
            const float s = c.hsvSaturationF();
            const float v = c.valueF();
            // grey, near black and near white say nothing about the cover's tone
            if (s < 0.18f || v < 0.18f)
                continue;
            const double weight = s * v;
            Bucket &bucket = buckets[static_cast<size_t>(qMax(0.0f, c.hsvHueF()) * kHueBuckets) % kHueBuckets];
            bucket.weight += weight;
            bucket.r += c.redF() * weight;
            bucket.g += c.greenF() * weight;
            bucket.b += c.blueF() * weight;
        }
    }
    const auto best = std::max_element(buckets.cbegin(), buckets.cend(),
                                       [](const Bucket &a, const Bucket &b) { return a.weight < b.weight; });
    // a few colorful pixels are no theme
    if (best->weight < kSample * kSample * 0.02)
        return {};
    return QColor::fromRgbF(best->r / best->weight, best->g / best->weight, best->b / best->weight);
}

void ColorTheme::onTrackChanged()
{
    const QVariantMap track = m_playback->track();
    const QString videoId = track.value(QStringLiteral("videoId")).toString();
    const QString thumbnail = track.value(QStringLiteral("thumbnail")).toString();
    if (videoId == m_videoId || thumbnail.isEmpty())
        return;
    m_videoId = videoId;

    QNetworkRequest request(QUrl(ytm::artUrl(thumbnail, kCoverPixels)));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId] {
        reply->deleteLater();
        if (videoId != m_videoId)
            return;
        QImage cover;
        cover.loadFromData(reply->readAll());
        m_base = dominantColor(cover);
        apply();
        if (!cover.isNull())
            emit coverReady(videoId, cover);
    });
}

void ColorTheme::apply()
{
    fadeTo(m_settings->albumColors() && m_base.isValid() ? fromColor(m_base) : defaults());
}

void ColorTheme::fadeTo(const Palette &target)
{
    if (target.accent == m_to.accent && target.background == m_to.background)
        return;
    m_fade.stop();
    m_from = m_current;
    m_to = target;
    m_fade.start();
}
