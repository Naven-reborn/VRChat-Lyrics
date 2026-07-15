#include "win32_window.h"
#include "app_icon.h"
#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "Dwmapi.lib")

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// 1 = 关闭,2 = Mica,3 = Acrylic,4 = Tabbed
static const int kDwmAcrylic = 3;

namespace host {

static const wchar_t* kClassName = L"VrcLyricsCxxWindow";

LRESULT CALLBACK Win32Window::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto self = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (m_hook) {
        bool handled = false;
        LRESULT r = m_hook(hwnd, msg, wp, lp, handled);
        if (handled) return r;
    }
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            m_width  = LOWORD(lp);
            m_height = HIWORD(lp);
            m_resized = true;
        }
        return 0;
    case WM_NCCALCSIZE:
        // 整个 NC 区域吃掉 —— 不然 WS_THICKFRAME 会在窗口顶部画一条暗色 ~6px
        // 边框。Resize 和 Aero snap 不受影响(还是走 NCHITTEST 给 HTLEFT 那些)。
        if (wp == TRUE) return 0;
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        const int border = 6;
        bool left   = pt.x < border;
        bool right  = pt.x >= m_width - border;
        bool top    = pt.y < border;
        bool bottom = pt.y >= m_height - border;
        if (top && left)    return HTTOPLEFT;
        if (top && right)   return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right)return HTBOTTOMRIGHT;
        if (left)   return HTLEFT;
        if (right)  return HTRIGHT;
        if (top)    return HTTOP;
        if (bottom) return HTBOTTOM;
        if (pt.y < m_drag_strip_h && pt.x < m_width - m_title_btn_zone) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_CLOSE:
        // 默认也只置位退出标志,不 DestroyWindow。
        // 主循环看到 Closed() 后自己有序停线程再 Destroy(),避免关窗未响应。
        m_closed = true;
        return 0;
    case WM_DESTROY:
        m_closed = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

bool Win32Window::Create(const wchar_t* title, int width, int height) {
    WNDCLASSEX wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // 任务栏 / Alt-Tab / 窗口类图标 —— 嵌入的 VL app.ico。
    // 先 Unregister,避免热重载/重复启动时沿用旧 class 的默认图标。
    UnregisterClassW(kClassName, wc.hInstance);
    wc.hIcon   = LoadAppIcon(true);
    wc.hIconSm = LoadAppIcon(false);
    wc.lpszClassName = kClassName;
    RegisterClassEx(&wc);

    m_width = width;
    m_height = height;

    HMONITOR mon = MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfo(mon, &mi);
    int x = (mi.rcWork.left + mi.rcWork.right - width) / 2;
    int y = (mi.rcWork.top + mi.rcWork.bottom - height) / 2;

    m_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        kClassName,
        title,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        x, y, width, height,
        nullptr, nullptr, wc.hInstance, this);

    if (!m_hwnd) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    // Win11 系统圆角:ROUND = 默认圆角(约 8px),跟 UI 内 6/8px 圆角统一。
    // 老系统 / 不支持时 DwmSetWindowAttribute 会失败,窗口保持直角,无害。
    int corner_pref = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(m_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                          &corner_pref, sizeof(corner_pref));

    // 配合 WM_NCCALCSIZE=0 用 —— 给 DWM 一个 1px 的边框信号,它就会在窗口
    // 四周画系统级 drop shadow,但因为 client 覆盖整窗,看不到黑边。
    MARGINS margins{ 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // 图标必须在 ShowWindow 之前设好,否则任务栏会缓存默认空白图标。
    ApplyAppIcon(m_hwnd);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetForegroundWindow(m_hwnd);

    // Show 之后再设一次,覆盖 shell 可能刚缓存的默认图标。
    ApplyAppIcon(m_hwnd);
    return true;
}

bool Win32Window::Visible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void Win32Window::Show() {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void Win32Window::Hide() {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_HIDE);
}

void Win32Window::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClass(kClassName, GetModuleHandle(nullptr));
}

bool Win32Window::Resized() {
    bool r = m_resized;
    m_resized = false;
    return r;
}

void Win32Window::PumpMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) m_closed = true;
    }
}

}
