#include "Frost.h"
#include "Blur.h"
#include "Bridge.h"

#include <QImage>
#include <QThreadPool>

#include <chrono>
#include <cstring>
#include <memory>

namespace {

// the glass: blurMax 48 at full blur, saturation 0.2. A sixth of the
// window blurred by 2 is about as soft once Slint scales it back up.
constexpr int kScaleDown = 6;
constexpr int kRadius = 2;
constexpr float kSaturation = 0.2f;

void capture(const slint::ComponentWeakHandle<ui::MainWindow> &weak)
{
    auto window = weak.lock();
    if (!window)
        return;
    auto &frost = (*window)->global<ui::Frost>();
    // the overlays hide for this one render, which is not shown
    frost.set_capturing(true);
    auto shot = (*window)->window().take_snapshot();
    frost.set_capturing(false);
    if (!shot || shot->width() == 0 || shot->height() == 0)
        return;

    const int w = int(shot->width()), h = int(shot->height());
    const float scale = (*window)->window().scale_factor();
    QImage image(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; y++)
        std::memcpy(image.scanLine(y), shot->begin() + size_t(y) * w, size_t(w) * 4);

    QThreadPool::globalInstance()->start([weak, image, scale] {
        const BlurLook look { std::max(1, image.width() / kScaleDown), std::max(1, image.height() / kScaleDown),
                              kRadius, kSaturation, 0.0f, true };
        const QImage soft = blurred(image, look);
        std::vector<unsigned char> rgba(size_t(soft.width()) * soft.height() * 4);
        for (int y = 0; y < soft.height(); y++)
            std::memcpy(rgba.data() + size_t(y) * soft.width() * 4, soft.constScanLine(y), size_t(soft.width()) * 4);
        const float width = float(image.width()) / scale, height = float(image.height()) / scale;
        const int sw = soft.width(), sh = soft.height();
        slint::invoke_from_event_loop([weak, rgba = std::move(rgba), sw, sh, width, height] {
            auto window = weak.lock();
            if (!window)
                return;
            slint::SharedPixelBuffer<slint::Rgba8Pixel> buffer { uint32_t(sw), uint32_t(sh) };
            std::memcpy(buffer.begin(), rgba.data(), rgba.size());
            auto &frost = (*window)->global<ui::Frost>();
            frost.set_picture(slint::Image(buffer));
            frost.set_width(width);
            frost.set_height(height);
        });
    });
}

} // namespace

void startFrost(const slint::ComponentHandle<ui::MainWindow> &window)
{
    auto timer = std::make_shared<slint::Timer>();
    const slint::ComponentWeakHandle<ui::MainWindow> weak(window);
    window->global<ui::Frost>().on_refresh([timer, weak] {
        if (timer->running())
            return;
        // after the current event: the overlay asking is laid out by then
        timer->start(slint::TimerMode::SingleShot, std::chrono::milliseconds(0), [weak] { capture(weak); });
    });
    bridge::atUiShutdown([timer] { timer->stop(); });
}
