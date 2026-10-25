// The sign-in window on Linux: WebKitGTK 6 in a GTK 4 window, with an
// ephemeral network session (in memory only). Its default user agent, Safari's
// engine on Linux, is let past Google's sign-in (tested 2026-10-25 with
// --probe-email).
#include "Browser.h"

#include <QDateTime>
#include <QTimeZone>

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <memory>
#include <vector>

namespace browser {
namespace {

class GtkBrowser final : public Window
{
public:
    explicit GtkBrowser(const Options &options)
        : m_session(webkit_network_session_new_ephemeral())
        , m_loop(g_main_loop_new(nullptr, FALSE))
    {
        m_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", m_session, nullptr));
        std::vector<QByteArray> languages;
        for (const QString &l : options.languages)
            languages.push_back(l.toUtf8());
        std::vector<const char *> list;
        for (const QByteArray &l : languages)
            list.push_back(l.constData());
        list.push_back(nullptr);
        webkit_web_context_set_preferred_languages(webkit_web_view_get_context(m_view), list.data());

        m_window = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(m_window), options.title.toUtf8().constData());
        gtk_window_set_default_size(GTK_WINDOW(m_window), 520, 720);
        gtk_window_set_child(GTK_WINDOW(m_window), GTK_WIDGET(m_view));
        g_signal_connect(m_view, "load-changed", G_CALLBACK(+[](WebKitWebView *view, WebKitLoadEvent event, gpointer self) {
            auto *b = static_cast<GtkBrowser *>(self);
            if (event == WEBKIT_LOAD_FINISHED && b->pageLoaded)
                b->pageLoaded(QUrl(QString::fromUtf8(webkit_web_view_get_uri(view))));
        }), this);
        g_signal_connect(m_window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer self) -> gboolean {
            auto *b = static_cast<GtkBrowser *>(self);
            if (b->closed)
                b->closed();
            return TRUE;
        }), this);
    }

    ~GtkBrowser() override
    {
        gtk_window_destroy(GTK_WINDOW(m_window));
        g_object_unref(m_session);
        g_main_loop_unref(m_loop);
    }

    int exec(const QUrl &url)
    {
        webkit_web_view_load_uri(m_view, url.toString().toUtf8().constData());
        gtk_window_present(GTK_WINDOW(m_window));
        g_main_loop_run(m_loop);
        return m_code;
    }

    void cookies(std::function<void(const QList<QNetworkCookie> &)> done) override
    {
        using Done = std::function<void(const QList<QNetworkCookie> &)>;
        webkit_cookie_manager_get_all_cookies(webkit_network_session_get_cookie_manager(m_session), nullptr,
            +[](GObject *manager, GAsyncResult *result, gpointer data) {
                std::unique_ptr<Done> done(static_cast<Done *>(data));
                GList *all = webkit_cookie_manager_get_all_cookies_finish(WEBKIT_COOKIE_MANAGER(manager), result, nullptr);
                QList<QNetworkCookie> out;
                for (GList *i = all; i; i = i->next) {
                    auto *c = static_cast<SoupCookie *>(i->data);
                    QNetworkCookie cookie(soup_cookie_get_name(c), soup_cookie_get_value(c));
                    cookie.setDomain(QString::fromUtf8(soup_cookie_get_domain(c)));
                    cookie.setPath(QString::fromUtf8(soup_cookie_get_path(c)));
                    if (GDateTime *expires = soup_cookie_get_expires(c))
                        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(g_date_time_to_unix(expires), QTimeZone::UTC));
                    cookie.setSecure(soup_cookie_get_secure(c));
                    cookie.setHttpOnly(soup_cookie_get_http_only(c));
                    out.append(cookie);
                }
                g_list_free_full(all, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
                (*done)(out);
            }, new Done(std::move(done)));
    }

    void runJavaScript(const QString &script, std::function<void(const QString &)> done) override
    {
        using Done = std::function<void(const QString &)>;
        webkit_web_view_evaluate_javascript(m_view, script.toUtf8().constData(), -1, nullptr, nullptr, nullptr,
            +[](GObject *view, GAsyncResult *result, gpointer data) {
                std::unique_ptr<Done> done(static_cast<Done *>(data));
                QString text;
                if (JSCValue *value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(view), result, nullptr)) {
                    char *s = jsc_value_to_string(value);
                    text = QString::fromUtf8(s);
                    g_free(s);
                    g_object_unref(value);
                }
                (*done)(text);
            }, new Done(std::move(done)));
    }

    void after(int ms, std::function<void()> callback) override
    {
        using Callback = std::function<void()>;
        g_timeout_add(guint(ms), +[](gpointer data) -> gboolean {
            std::unique_ptr<Callback> callback(static_cast<Callback *>(data));
            (*callback)();
            return G_SOURCE_REMOVE;
        }, new Callback(std::move(callback)));
    }

    void quit(int code) override
    {
        m_code = code;
        g_main_loop_quit(m_loop);
    }

private:
    WebKitNetworkSession *m_session;
    GMainLoop *m_loop;
    WebKitWebView *m_view = nullptr;
    GtkWidget *m_window = nullptr;
    int m_code = 1;
};

} // namespace

int run(const Options &options, const std::function<void(Window &)> &setup)
{
    // the Wayland app id and X11 class, so the desktop's window rules apply
    g_set_prgname(options.appId.toUtf8().constData());
    if (!gtk_init_check())
        return 2;
    GtkBrowser browser(options);
    setup(browser);
    return browser.exec(options.url);
}

} // namespace browser
