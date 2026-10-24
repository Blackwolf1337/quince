#pragma once

#include "Bridge.h"

class AppController;
class ImageCache;
class SlintHost;
class VideoRenderer;

// Core thread, once the AppController exists: keeps the Slint globals in step
// with the core and hands the Slint callbacks to the core thread. The objects
// passed in live on the Slint thread and outlive the core's event loop.
void bindCore(AppController &app, const bridge::Window &window, const bridge::Tray &tray, ImageCache *images,
              VideoRenderer *video, SlintHost *host);
