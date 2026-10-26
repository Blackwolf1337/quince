#pragma once

#include "Bridge.h"

#include <QObject>
#include <QStringList>

class AppController;

// Replays a script of user input into the Slint window for test runs
// (--steps). Lives on the core thread; the input
// goes to the Slint thread as window events. Commands, one per line:
//   wait <ms> | click <x> <y> [right] [ctrl|shift] | move <x> <y> | scroll <x> <y> <dy>
//   type <text> | key <Return|Escape|Down|Up|Space|Backspace|Tab>
//   shot <file> | size | selection | quit, and the window-less ones of app/TestCommands.h
class TestDriver : public QObject
{
public:
    TestDriver(bridge::Window window, AppController *app, const QStringList &lines, QObject *parent = nullptr);

    void start();

    // Slint thread: a picture of the window (without the video under it), as
    // PNG written on the thread of coreObject
    static void screenshot(ui::MainWindow &w, const QString &file, QObject *coreObject);

private:
    void next();
    void run(const QString &line);

    bridge::Window m_window;
    AppController *m_app = nullptr;
    QStringList m_lines;
    int m_pos = 0;
};
