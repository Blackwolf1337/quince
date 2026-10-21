#include "player/MpvVideoItem.h"
#include "player/MpvPlayer.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <mpv/client.h>
#include <mpv/render_gl.h>

Q_LOGGING_CATEGORY(lcVideo, "quince.video")

namespace {

void *procAddress(void *, const char *name)
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void *>(context->getProcAddress(name)) : nullptr;
}

// Lives on the render thread; owns the render context of one mpv instance.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvVideoItem *item)
        : m_item(item)
    {
    }

    ~MpvRenderer() override { release(); }

    // GUI thread blocked: which mpv instance to draw
    void synchronize(QQuickFramebufferObject *item) override
    {
        auto *video = static_cast<MpvVideoItem *>(item);
        m_player = video->player();
        m_wanted = m_player ? m_player->renderHandle() : nullptr;
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        return new QOpenGLFramebufferObject(size, format);
    }

    void render() override
    {
        if (m_handle != m_wanted) {
            release();
            if (m_wanted)
                create(m_wanted);
        }
        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!m_context || !fbo)
            return;
        // the API wants this after every update callback; mpv waits for it
        // before it hands out the next frame
        mpv_render_context_update(m_context);
        mpv_opengl_fbo target{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flip = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &target},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_context, params);
    }

private:
    void create(mpv_handle *handle)
    {
        mpv_opengl_init_params gl{&procAddress, nullptr};
        const char *api = MPV_RENDER_API_TYPE_OPENGL;
        int advanced = 1;
        QList<mpv_render_param> params{
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(api)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        };
        // the display lets mpv share decoded frames with the GPU (hwdec interop);
        // only Linux builds of Qt have these interfaces
        bool display = false;
#if QT_CONFIG(wayland)
        if (auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            params.append({MPV_RENDER_PARAM_WL_DISPLAY, wayland->display()});
            display = true;
        }
#endif
#if QT_CONFIG(xcb)
        if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>(); x11 && !display)
            params.append({MPV_RENDER_PARAM_X11_DISPLAY, x11->display()});
#endif
        Q_UNUSED(display)
        params.append({MPV_RENDER_PARAM_INVALID, nullptr});

        if (mpv_render_context_create(&m_context, handle, params.data()) < 0) {
            qCWarning(lcVideo) << "no mpv render context";
            m_context = nullptr;
            return;
        }
        m_handle = handle;
        mpv_render_context_set_update_callback(m_context, &MpvRenderer::onUpdate, m_item);
        qCDebug(lcVideo) << "render context ready";
        // only now may mpv open its video output
        setOutput(true);
        // and a first frame so mpv sees the context in use
        onUpdate(m_item);
    }

    void setOutput(bool on)
    {
        if (QPointer<MpvPlayer> player = m_player)
            QMetaObject::invokeMethod(player, [player, on] {
                if (player)
                    player->setVideoOutput(on);
            }, Qt::QueuedConnection);
    }

    void release()
    {
        if (m_context) {
            setOutput(false);
            mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
            mpv_render_context_free(m_context);
        }
        m_context = nullptr;
        m_handle = nullptr;
    }

    // any mpv thread: a new frame is there
    static void onUpdate(void *ctx)
    {
        QMetaObject::invokeMethod(static_cast<MpvVideoItem *>(ctx), &QQuickItem::update, Qt::QueuedConnection);
    }

    MpvVideoItem *m_item = nullptr;
    QPointer<MpvPlayer> m_player;
    mpv_handle *m_wanted = nullptr;
    mpv_handle *m_handle = nullptr;
    mpv_render_context *m_context = nullptr;
};

} // namespace

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
}

void MpvVideoItem::setPlayer(MpvPlayer *player)
{
    if (m_player == player)
        return;
    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);
    m_player = player;
    // after a crossfade the other instance draws
    if (m_player)
        connect(m_player, &MpvPlayer::activeDeckChanged, this, &QQuickItem::update);
    update();
    emit playerChanged();
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}
