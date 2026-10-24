#pragma once

#include "main.h"

// Slint thread, once: answers Frost.refresh() with a new blurred picture of
// the window (ui/frost.slint). Requests in one turn of the event loop make
// one picture.
void startFrost(const slint::ComponentHandle<ui::MainWindow> &window);
