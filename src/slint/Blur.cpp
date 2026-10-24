#include "Blur.h"

#include <algorithm>
#include <vector>

namespace {

constexpr int kPasses = 3;
constexpr int kChannels = 4;

// one box blur of the planes along x or y, edges clamped
void boxBlur(const std::vector<float> &src, std::vector<float> &dst, int w, int h, int radius, bool alongX)
{
    const int len = alongX ? w : h, lines = alongX ? h : w;
    const int step = alongX ? kChannels : w * kChannels;
    for (int line = 0; line < lines; line++) {
        const int base = alongX ? line * w * kChannels : line * kChannels;
        for (int c = 0; c < kChannels; c++) {
            const auto at = [&](int i) { return src[size_t(base + std::clamp(i, 0, len - 1) * step + c)]; };
            float sum = 0;
            for (int i = -radius; i <= radius; i++)
                sum += at(i);
            for (int i = 0; i < len; i++) {
                dst[size_t(base + i * step + c)] = sum / float(2 * radius + 1);
                sum += at(i + radius + 1) - at(i - radius);
            }
        }
    }
}

} // namespace

QImage blurred(const QImage &source, const BlurLook &look)
{
    QImage image = source.scaled(look.width, look.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGBA8888);
    const int w = image.width(), h = image.height();
    std::vector<float> plane(size_t(w) * size_t(h) * kChannels), tmp(plane.size());
    for (int y = 0; y < h; y++) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < w * kChannels; x++)
            plane[size_t(y * w * kChannels + x)] = line[x] / 255.0f;
    }
    for (int i = 0; i < kPasses; i++) {
        boxBlur(plane, tmp, w, h, look.radius, true);
        boxBlur(tmp, plane, w, h, look.radius, false);
    }
    for (int y = 0; y < h; y++) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < w; x++) {
            const float *p = &plane[size_t((y * w + x) * kChannels)];
            const float gray = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            for (int c = 0; c < 3; c++) {
                const float v = gray + (p[c] - gray) * look.saturation + look.brightness;
                line[x * kChannels + c] = uchar(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            line[x * kChannels + 3] = look.keepAlpha ? uchar(std::clamp(p[3], 0.0f, 1.0f) * 255.0f + 0.5f) : 255;
        }
    }
    return image;
}
