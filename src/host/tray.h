#pragma once
#include <Windows.h>
#include <functional>

namespace host {

class TrayIcon {
public:
    static const UINT kMsgId = WM_USER + 1;

    bool Install(HWND owner, const wchar_t* tip);
    void Remove();
    void UpdateTip(const wchar_t* tip);

    // Forward WM_USER+1 / TaskbarCreated from the host wndproc/hook.
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    std::function<void()> OnShow;
    std::function<void()> OnQuit;

private:
    HWND m_owner = nullptr;
    bool m_installed = false;
};

}
