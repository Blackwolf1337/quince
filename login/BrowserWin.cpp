// The sign-in window on Windows: WebView2 (the Edge engine that Windows 10
// and 11 bring) in InPrivate mode, in a plain Win32 window. WebView2Loader.dll
// from the WebView2 SDK lies next to the program and is loaded at run time.
#include "Browser.h"

#include <QDateTime>
#include <QDir>
#include <QTimeZone>

#include <windows.h>
#include <WebView2.h>
#include <wrl/client.h>

#include <map>

namespace browser {
namespace {

using CreateEnvironment = HRESULT(STDAPICALLTYPE *)(PCWSTR browserFolder, PCWSTR userDataFolder,
                                                     ICoreWebView2EnvironmentOptions *options,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

// A WebView2 callback around a function (COM object, one thread).
template<typename Interface, const IID &Iid, typename... Args>
class Callback final : public Interface
{
public:
    explicit Callback(std::function<HRESULT(Args...)> f) : m_f(std::move(f)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (riid != IID_IUnknown && riid != Iid) {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        *object = static_cast<Interface *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return m_f(args...); }

private:
    ULONG m_refs = 1;
    std::function<HRESULT(Args...)> m_f;
};

// a callback for one WebView2 call, which keeps its own reference
template<typename T, typename F>
Microsoft::WRL::ComPtr<T> make(F f)
{
    Microsoft::WRL::ComPtr<T> callback;
    callback.Attach(new T(std::move(f)));
    return callback;
}

using EnvironmentDone = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                                 IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment *>;
using ControllerDone = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller *>;
using NavigationDone = Callback<ICoreWebView2NavigationCompletedEventHandler, IID_ICoreWebView2NavigationCompletedEventHandler,
                                ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *>;
using CookiesDone = Callback<ICoreWebView2GetCookiesCompletedHandler, IID_ICoreWebView2GetCookiesCompletedHandler, HRESULT,
                             ICoreWebView2CookieList *>;
using ScriptDone = Callback<ICoreWebView2ExecuteScriptCompletedHandler, IID_ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>;

QString fromWide(LPCWSTR text)
{
    return text ? QString::fromWCharArray(text) : QString();
}

// a string WebView2 allocated, freed after reading
QString takeString(LPWSTR text)
{
    const QString s = fromWide(text);
    CoTaskMemFree(text);
    return s;
}

class WinBrowser final : public Window
{
public:
    explicit WinBrowser(const Options &options) : m_options(options) {}

    ~WinBrowser() override
    {
        if (m_controller) {
            m_controller->Close();
            m_controller->Release();
        }
        if (m_view)
            m_view->Release();
        if (m_window)
            DestroyWindow(m_window);
        // the profile folder (InPrivate keeps little in it); the browser's
        // processes may still hold files, so as much as goes
        if (!m_dataFolder.isEmpty())
            QDir(m_dataFolder).removeRecursively();
    }

    bool create()
    {
        HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
        auto createEnvironment = loader ? reinterpret_cast<CreateEnvironment>(
                                              reinterpret_cast<void *>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")))
                                        : nullptr;
        if (!createEnvironment)
            return false;

        WNDCLASSW cls {};
        cls.lpfnWndProc = &WinBrowser::proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        cls.hIcon = LoadIconW(cls.hInstance, L"IDI_ICON1");
        cls.lpszClassName = L"QuinceLogin";
        RegisterClassW(&cls);
        const double scale = GetDpiForSystem() / 96.0;
        m_window = CreateWindowExW(0, cls.lpszClassName, reinterpret_cast<LPCWSTR>(m_options.title.utf16()),
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, int(520 * scale), int(720 * scale),
                                   nullptr, nullptr, cls.hInstance, this);
        if (!m_window)
            return false;

        m_dataFolder = QDir::temp().filePath(QStringLiteral("quince-login-%1").arg(GetCurrentProcessId()));
        const HRESULT hr = createEnvironment(nullptr, reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(m_dataFolder).utf16()),
                                             nullptr, make<EnvironmentDone>([this](HRESULT result, ICoreWebView2Environment *env) {
            if (FAILED(result) || !env) {
                quit(2);
                return S_OK;
            }
            createController(env);
            return S_OK;
        }).Get());
        return SUCCEEDED(hr);
    }

    int exec()
    {
        ShowWindow(m_window, SW_SHOWNORMAL);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return int(msg.wParam);
    }

    void cookies(std::function<void(const QList<QNetworkCookie> &)> done) override
    {
        ICoreWebView2_2 *view2 = nullptr;
        ICoreWebView2CookieManager *manager = nullptr;
        if (!m_view || FAILED(m_view->QueryInterface(IID_ICoreWebView2_2, reinterpret_cast<void **>(&view2)))) {
            done({});
            return;
        }
        view2->get_CookieManager(&manager);
        view2->Release();
        if (!manager) {
            done({});
            return;
        }
        manager->GetCookies(L"", make<CookiesDone>([done](HRESULT, ICoreWebView2CookieList *list) {
            QList<QNetworkCookie> out;
            UINT count = 0;
            if (list)
                list->get_Count(&count);
            for (UINT i = 0; i < count; i++) {
                ICoreWebView2Cookie *c = nullptr;
                if (FAILED(list->GetValueAtIndex(i, &c)) || !c)
                    continue;
                LPWSTR text = nullptr;
                c->get_Name(&text);
                const QByteArray name = takeString(text).toUtf8();
                c->get_Value(&text);
                QNetworkCookie cookie(name, takeString(text).toUtf8());
                c->get_Domain(&text);
                cookie.setDomain(takeString(text));
                c->get_Path(&text);
                cookie.setPath(takeString(text));
                BOOL flag = FALSE;
                double expires = -1;
                if (SUCCEEDED(c->get_IsSession(&flag)) && !flag && SUCCEEDED(c->get_Expires(&expires)) && expires > 0)
                    cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(qint64(expires), QTimeZone::UTC));
                c->get_IsSecure(&flag);
                cookie.setSecure(flag);
                c->get_IsHttpOnly(&flag);
                cookie.setHttpOnly(flag);
                out.append(cookie);
                c->Release();
            }
            done(out);
            return S_OK;
        }).Get());
        manager->Release();
    }

    void runJavaScript(const QString &script, std::function<void(const QString &)> done) override
    {
        if (!m_view) {
            done({});
            return;
        }
        m_view->ExecuteScript(reinterpret_cast<LPCWSTR>(script.utf16()), make<ScriptDone>([done](HRESULT, LPCWSTR json) {
            done(fromWide(json));
            return S_OK;
        }).Get());
    }

    void after(int ms, std::function<void()> callback) override
    {
        const UINT_PTR id = ++m_nextTimer;
        m_timers[id] = std::move(callback);
        SetTimer(m_window, id, UINT(ms), nullptr);
    }

    void quit(int code) override { PostQuitMessage(code); }

private:
    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams));
        auto *self = reinterpret_cast<WinBrowser *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self) {
            switch (message) {
            case WM_SIZE:
                self->fit();
                return 0;
            case WM_CLOSE:
                if (self->closed)
                    self->closed();
                return 0;
            case WM_TIMER: {
                KillTimer(window, wParam);
                auto it = self->m_timers.find(wParam);
                if (it != self->m_timers.end()) {
                    const std::function<void()> callback = std::move(it->second);
                    self->m_timers.erase(it);
                    callback();
                }
                return 0;
            }
            default:
                break;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void createController(ICoreWebView2Environment *env)
    {
        auto done = make<ControllerDone>([this](HRESULT result, ICoreWebView2Controller *controller) {
            if (FAILED(result) || !controller) {
                quit(2);
                return S_OK;
            }
            m_controller = controller;
            m_controller->AddRef();
            m_controller->get_CoreWebView2(&m_view);
            fit();
            EventRegistrationToken token;
            m_view->add_NavigationCompleted(make<NavigationDone>([this](ICoreWebView2 *view, ICoreWebView2NavigationCompletedEventArgs *) {
                LPWSTR source = nullptr;
                view->get_Source(&source);
                if (pageLoaded)
                    pageLoaded(QUrl(takeString(source)));
                return S_OK;
            }).Get(), &token);
            m_view->Navigate(reinterpret_cast<LPCWSTR>(m_options.url.toString().utf16()));
            return S_OK;
        });
        // InPrivate (WebView2 runtime 1.0.1245): no profile left behind
        ICoreWebView2Environment10 *env10 = nullptr;
        ICoreWebView2ControllerOptions *options = nullptr;
        if (SUCCEEDED(env->QueryInterface(IID_ICoreWebView2Environment10, reinterpret_cast<void **>(&env10)))
            && SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&options))) {
            options->put_IsInPrivateModeEnabled(TRUE);
            env10->CreateCoreWebView2ControllerWithOptions(m_window, options, done.Get());
            options->Release();
        } else {
            env->CreateCoreWebView2Controller(m_window, done.Get());
        }
        if (env10)
            env10->Release();
    }

    // the browser fills the window
    void fit()
    {
        if (!m_controller)
            return;
        RECT bounds;
        GetClientRect(m_window, &bounds);
        m_controller->put_Bounds(bounds);
    }

    Options m_options;
    HWND m_window = nullptr;
    QString m_dataFolder;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_view = nullptr;
    std::map<UINT_PTR, std::function<void()>> m_timers;
    UINT_PTR m_nextTimer = 0;
};

} // namespace

int run(const Options &options, const std::function<void(Window &)> &setup)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // WebView2 wants a single-threaded apartment
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WinBrowser browser(options);
    if (!browser.create()) {
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(options.noBrowser.utf16()),
                    reinterpret_cast<LPCWSTR>(options.title.utf16()), MB_ICONERROR);
        return 2;
    }
    setup(browser);
    return browser.exec();
}

} // namespace browser
