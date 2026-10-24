#pragma once

#include "app/AppController.h"

#include <QString>

class QCoreApplication;

// The options both frontends take; test options only matter to test runs.
struct CommandLine
{
    AppOptions app;
    int quitAfterMs = -1; // quit after this long (test runs)
    QString screenshot;   // picture of the window before quitting (test runs)
    QString steps;        // input script to replay (test runs)
    QString appId;        // Wayland app id / X11 class
};

// The process arguments; exits on --help and --version like Qt's parser.
CommandLine parseCommandLine(const QCoreApplication &app);
