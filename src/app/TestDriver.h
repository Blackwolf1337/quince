#pragma once

#include <QObject>
#include <QStringList>

class QQuickWindow;
class AppController;

// Replays a script of user input into the window for test runs (--steps).
// One command per line, run one after another on timers:
//   wait <ms> | click <x> <y> [right] | move <x> <y> | scroll <x> <y> <dy>
//   type <text> | key <Return|Escape|Down|Up|Space|Backspace|Tab>
//   open <browseId> | shot <file> | state | size | sh <command> | quit
//   call <App property or .> <method> [string argument]
class TestDriver : public QObject
{
    Q_OBJECT

public:
    TestDriver(QQuickWindow *window, AppController *app, const QStringList &lines, QObject *parent = nullptr);

    void start();

private:
    void next();
    void run(const QString &line);
    void mouse(int x, int y, Qt::MouseButton button);
    void key(int key, const QString &text = {});

    QQuickWindow *m_window = nullptr;
    AppController *m_app = nullptr;
    QStringList m_lines;
    int m_pos = 0;
};
