#include "TestDriver.h"
#include "app/AppController.h"
#include "app/TestCommands.h"

#include <QDebug>
#include <QHash>
#include <QImage>
#include <QTimer>

#include <cstring>
#include <string_view>

namespace {

// pause between two commands, so the scene can react
constexpr int kStepMs = 150;
// one wheel notch in logical pixels
constexpr float kNotch = 40.f;

slint::SharedString keyText(std::u8string_view key)
{
    return slint::SharedString(std::string_view(reinterpret_cast<const char *>(key.data()), key.size()));
}

} // namespace

TestDriver::TestDriver(bridge::Window window, AppController *app, const QStringList &lines, QObject *parent)
    : QObject(parent)
    , m_window(std::move(window))
    , m_app(app)
    , m_lines(lines)
{
}

void TestDriver::start()
{
    QTimer::singleShot(kStepMs, this, &TestDriver::next);
}

void TestDriver::next()
{
    while (m_pos < m_lines.size()) {
        const QString line = m_lines.at(m_pos++).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u"wait ")) {
            QTimer::singleShot(line.mid(5).toInt(), this, &TestDriver::next);
            return;
        }
        run(line);
        QTimer::singleShot(kStepMs, this, &TestDriver::next);
        return;
    }
}

void TestDriver::screenshot(ui::MainWindow &w, const QString &file, QObject *coreObject)
{
    auto shot = w.window().take_snapshot();
    if (!shot)
        return;
    const int width = int(shot->width()), height = int(shot->height());
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; y++)
        std::memcpy(image.scanLine(y), shot->begin() + size_t(y) * width, size_t(width) * 4);
    // Slint's pixels were copied; the PNG is written off the Slint thread
    bridge::toCore(coreObject, [image, file] { image.save(file); });
}

void TestDriver::run(const QString &line)
{
    if (runTestCommand(m_app, line, this))
        return;
    const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
    const QString command = parts.value(0);
    using namespace slint::platform;

    if (command == u"click" || command == u"move") {
        const slint::LogicalPosition pos({ parts.value(1).toFloat(), parts.value(2).toFloat() });
        const bool click = command == u"click";
        const auto button = parts.value(3) == u"right" ? slint::PointerEventButton::Right : slint::PointerEventButton::Left;
        bridge::toUi(m_window, [pos, click, button](ui::MainWindow &w) {
            w.window().dispatch_pointer_move_event(pos);
            if (!click)
                return;
            w.window().dispatch_pointer_press_event(pos, button);
            w.window().dispatch_pointer_release_event(pos, button);
        });
    } else if (command == u"scroll") {
        // like a mouse wheel: QML's notches of 120 as Slint's logical pixels
        const slint::LogicalPosition pos({ parts.value(1).toFloat(), parts.value(2).toFloat() });
        const float dy = parts.value(3).toFloat() / 120.f * kNotch;
        bridge::toUi(m_window, [pos, dy](ui::MainWindow &w) {
            w.window().dispatch_pointer_move_event(pos);
            w.window().dispatch_pointer_scroll_event(pos, 0.f, dy);
        });
    } else if (command == u"type") {
        const std::string text = line.mid(5).toStdString();
        bridge::toUi(m_window, [text](ui::MainWindow &w) {
            const QString chars = QString::fromStdString(text);
            for (const QChar c : chars) {
                const slint::SharedString s(QString(c).toStdString());
                w.window().dispatch_key_press_event(s);
                w.window().dispatch_key_release_event(s);
            }
        });
    } else if (command == u"key") {
        static const QHash<QString, std::u8string_view> keys{
            {QStringLiteral("Return"), key_codes::Return},       {QStringLiteral("Escape"), key_codes::Escape},
            {QStringLiteral("Down"), key_codes::DownArrow},      {QStringLiteral("Up"), key_codes::UpArrow},
            {QStringLiteral("Space"), key_codes::Space},         {QStringLiteral("Backspace"), key_codes::Backspace},
            {QStringLiteral("Tab"), key_codes::Tab},
        };
        if (!keys.contains(parts.value(1))) {
            qWarning() << "test driver: unknown key" << parts.value(1);
            return;
        }
        const slint::SharedString key = keyText(keys.value(parts.value(1)));
        bridge::toUi(m_window, [key](ui::MainWindow &w) {
            w.window().dispatch_key_press_event(key);
            w.window().dispatch_key_release_event(key);
        });
    } else if (command == u"shot") {
        bridge::toUi(m_window, [file = parts.value(1), self = this](ui::MainWindow &w) { screenshot(w, file, self); });
    } else if (command == u"size") {
        bridge::toUi(m_window, [](ui::MainWindow &w) {
            const auto size = w.window().size();
            qInfo("window %ux%u scale %.2f", size.width, size.height, double(w.window().scale_factor()));
        });
    } else if (command == u"quit") {
        bridge::toUi(m_window, [](ui::MainWindow &) { slint::quit_event_loop(); });
    } else {
        qWarning() << "test driver: unknown command" << line;
    }
}
