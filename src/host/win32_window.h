#pragma once
#include <Windows.h>
#include <functional>

namespace host {

class Win32Window {
public:
    using MessageHook = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, bool&)>;

    bool Create(const wchar_t* title, int width, int height);
    void Destroy();

    HWND Hwnd() const { return m_hwnd; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    bool Closed() const { return m_closed; }
    void Quit() { m_closed = true; }
    bool Resized();
    bool Visible() const;
    void Show();
    void Hide();

    void PumpMessages();
    void SetMessageHook(MessageHook hook) { m_hook = std::move(hook); }

    void SetDragRegion(int height_px) { m_drag_strip_h = height_px; }
    // How many pixels on the right edge of the drag region are NOT draggable
    // (so close/minimize/icon buttons can receive clicks).
    void SetTitleButtonZone(int px) { m_title_btn_zone = px; }

    // Win11 Acrylic backdrop. enable=true 时 DWMWA_SYSTEMBACKDROP_TYPE=Acrylic。
    // 返回是否真正生效(老系统会失败并保持不透明)。
    bool SetAcrylicBlur(bool enable);
    bool AcrylicEnabled() const { return m_acrylic; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND m_hwnd = nullptr;
    int m_width = 0, m_height = 0;
    bool m_closed = false;
    bool m_resized = false;
    int m_drag_strip_h = 40;
    int m_title_btn_zone = 180;
    bool m_acrylic = false;
    MessageHook m_hook;
};

}
