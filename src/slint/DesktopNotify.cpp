#include "DesktopNotify.h"

#if defined(_WIN32)

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

// the icon of the bubble, alone in the notification area while it shows
constexpr UINT kIconId = 1;
constexpr UINT kBubbleMs = 5000;

HWND messageWindow()
{
    static HWND window = [] {
        WNDCLASSW cls {};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"QuinceNotify";
        RegisterClassW(&cls);
        return CreateWindowW(cls.lpszClassName, L"Quince", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance,
                             nullptr);
    }();
    return window;
}

// the cover as an icon for the bubble's large picture
HICON iconOf(const QImage &cover)
{
    if (cover.isNull())
        return nullptr;
    const QImage image = cover.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    BITMAPV5HEADER header {};
    header.bV5Size = sizeof(header);
    header.bV5Width = image.width();
    header.bV5Height = -image.height();
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_RGB;
    void *bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return nullptr;
    for (int y = 0; y < image.height(); y++)
        memcpy(static_cast<uchar *>(bits) + size_t(y) * image.width() * 4, image.constScanLine(y), size_t(image.width()) * 4);
    HBITMAP mask = CreateBitmap(image.width(), image.height(), 1, 1, nullptr);
    ICONINFO info { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void copy(wchar_t *target, size_t size, const QString &text)
{
    const std::wstring w = text.left(int(size) - 1).toStdWString();
    wcsncpy(target, w.c_str(), size - 1);
    target[size - 1] = 0;
}

} // namespace

void desktopNotify(const QString &title, const QString &body, const QImage &cover)
{
    HWND window = messageWindow();
    if (!window)
        return;
    NOTIFYICONDATAW data {};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kIconId;
    // the previous bubble's icon goes first: a new one replaces it
    Shell_NotifyIconW(NIM_DELETE, &data);
    data.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_ICON1");
    copy(data.szTip, std::size(data.szTip), QStringLiteral("Quince"));
    copy(data.szInfoTitle, std::size(data.szInfoTitle), title);
    copy(data.szInfo, std::size(data.szInfo), body);
    data.uTimeout = kBubbleMs;
    HICON large = iconOf(cover);
    data.dwInfoFlags = large ? NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND : NIIF_INFO | NIIF_NOSOUND;
    data.hBalloonIcon = large;
    Shell_NotifyIconW(NIM_ADD, &data);
    if (large)
        DestroyIcon(large);
}

#elif !defined(__APPLE__)

// Linux sends notifications over D-Bus in the core (integrations/Notifications)
void desktopNotify(const QString &, const QString &, const QImage &) {}

#endif
