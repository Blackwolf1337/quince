#pragma once

#include <QImage>

// A soft picture for backdrops and frosted glass: scaled down to width x
// height (Slint scales it up smoothly when drawn), three box blurs of the
// given radius (nearly a Gaussian), then saturation and brightness as Qt Quick's
// MultiEffect has them (saturation 1 keeps the colors, brightness 0 the
// light). keepAlpha blurs the alpha channel too (see-through windows), else
// the result is opaque. Any thread.
struct BlurLook
{
    int width = 64;
    int height = 64;
    int radius = 5;
    float saturation = 1.0f;
    float brightness = 0.0f;
    bool keepAlpha = false;
};

QImage blurred(const QImage &source, const BlurLook &look);
