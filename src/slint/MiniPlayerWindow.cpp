#include "MiniPlayerWindow.h"
#include "VideoRenderer.h"

#include <algorithm>
#include <cstdio>

namespace {

// a video makes it wide (340 x 240 as designed), the cover square
constexpr float kVideoAspect = 240.0f / 340.0f;
constexpr int kMinWidth = 220;

mini::MiniState toMini(const ui::MiniState &s)
{
    mini::MiniState m;
    m.title = s.title;
    m.subtitle = s.subtitle;
    m.art = s.art;
    m.playing = s.playing;
    m.has_next = s.has_next;
    m.position = s.position;
    m.duration = s.duration;
    m.like_status = s.like_status;
    m.can_rate = s.can_rate;
    m.picture = s.picture;
    m.video = s.video;
    m.restarts = s.restarts;
    m.accent = s.accent;
    m.background = s.background;
    return m;
}

} // namespace

MiniPlayerWindow::MiniPlayerWindow(bridge::Window main, VideoRenderer *mainVideo)
    : m_main(std::move(main))
    , m_mainVideo(mainVideo)
{
}

MiniPlayerWindow::~MiniPlayerWindow() = default;

void MiniPlayerWindow::open()
{
    auto main = m_main.lock();
    if (!main || m_open)
        return;
    m_open = true;
    (*main)->global<ui::App>().set_mini_player_open(true);
    m_mainVideo->release([weak = weak_from_this()] {
        if (auto self = weak.lock())
            self->show();
    });
}

void MiniPlayerWindow::show()
{
    auto main = m_main.lock();
    if (!main || !m_open)
        return;
    const bool fresh = !m_window;
    if (!m_window) {
        m_window = mini::MiniPlayer::create();
        auto &w = **m_window;
        m_video = std::make_unique<VideoRenderer>(slint::ComponentWeakHandle<mini::MiniPlayer>(*m_window));
        if (w.window().set_rendering_notifier(std::ref(*m_video)))
            std::fprintf(stderr, "quince: the mini player renders without OpenGL, no video\n");
        m_video->onOutput = m_mainVideo->onOutput;
        m_video->setSource(m_source);

        // the transport goes through the main window's callbacks
        const bridge::Window target = m_main;
        auto player = [target](auto f) {
            return [target, f](auto... args) {
                if (auto m = target.lock())
                    f((*m)->global<ui::Player>(), args...);
            };
        };
        w.on_toggle_pause(player([](const ui::Player &p) { p.invoke_toggle_pause(); }));
        w.on_previous(player([](const ui::Player &p) { p.invoke_previous(); }));
        w.on_next(player([](const ui::Player &p) { p.invoke_next(); }));
        w.on_rate(player([](const ui::Player &p, const slint::SharedString &s) { p.invoke_rate(s); }));

        auto weak = weak_from_this();
        w.on_restore([weak] {
            if (auto self = weak.lock())
                self->close();
        });
        w.on_width_chosen([weak](int width) {
            auto self = weak.lock();
            if (self && self->m_open && width >= kMinWidth) {
                self->m_width = width;
                if (self->onWidthChosen)
                    self->onWidthChosen(width);
            }
        });
        w.window().on_close_requested([weak] {
            if (auto self = weak.lock())
                self->close();
            return slint::CloseRequestResponse::KeepWindowShown;
        });
    }
    apply();
    const float width = float(std::max(m_width, kMinWidth));
    // a set_size before winit made the window crashed it on Windows: the first
    // time the size goes in as the preferred one
    if (fresh) {
        (*m_window)->set_start_width(width);
        (*m_window)->set_start_height(heightFor(width));
    } else {
        (*m_window)->window().set_size(slint::LogicalSize({ width, heightFor(width) }));
    }
    m_video->resume();
    (*m_window)->show();
    (*main)->hide();
}

void MiniPlayerWindow::close()
{
    if (!m_open || !m_video)
        return;
    m_open = false;
    m_video->release([weak = weak_from_this()] {
        auto self = weak.lock();
        if (!self)
            return;
        if (auto main = self->m_main.lock()) {
            (*main)->global<ui::App>().set_mini_player_open(false);
            self->m_mainVideo->resume();
            (*main)->show();
        }
        if (self->m_window)
            (*self->m_window)->hide();
    });
}

void MiniPlayerWindow::destroy()
{
    // the window takes its render context with it, then the renderer may go
    if (m_window)
        (*m_window)->hide();
    m_window.reset();
    m_video.reset();
}

void MiniPlayerWindow::setState(const ui::MiniState &state)
{
    const bool resize = m_open && m_window && state.video != m_state.video;
    m_state = state;
    if (!m_window)
        return;
    apply();
    if (resize) {
        const auto &window = (*m_window)->window();
        const float width = window.size().width / window.scale_factor();
        (*m_window)->window().set_size(slint::LogicalSize({ width, heightFor(width) }));
    }
}

void MiniPlayerWindow::setSource(mpv_handle *mpv)
{
    m_source = mpv;
    if (m_video)
        m_video->setSource(mpv);
}

void MiniPlayerWindow::apply()
{
    auto &w = **m_window;
    w.set_state(toMini(m_state));
    auto &style = w.global<mini::Style>();
    style.set_accent(m_state.accent);
    style.set_background(m_state.background);
}

float MiniPlayerWindow::heightFor(float width) const
{
    return m_state.video ? float(int(width * kVideoAspect + 0.5f)) : width;
}
