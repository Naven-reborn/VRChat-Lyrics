#include "tray.h"
#include <shellapi.h>
#include <cstring>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace host {

static UINT g_taskbar_created_msg = 0;

static NOTIFYICONDATAW MakeNid(HWND owner, const wchar_t* tip) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TrayIcon::kMsgId;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (tip) {
        wcsncpy_s(nid.szTip, tip, _TRUNCATE);
    }
    return nid;
}

bool TrayIcon::Install(HWND owner, const wchar_t* tip) {
    m_owner = owner;
    if (g_taskbar_created_msg == 0)
        g_taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");

    NOTIFYICONDATAW nid = MakeNid(owner, tip);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    m_installed = true;
    return true;
}

void TrayIcon::Remove() {
    if (!m_installed) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_owner;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_installed = false;
}

void TrayIcon::UpdateTip(const wchar_t* tip) {
    if (!m_installed) return;
    NOTIFYICONDATAW nid = MakeNid(m_owner, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool TrayIcon::HandleMessage(UINT msg, WPARAM /*wp*/, LPARAM lp) {
    // explorer.exe 重启会广播 TaskbarCreated,这时图标得重新挂一次。
    if (g_taskbar_created_msg && msg == g_taskbar_created_msg) {
        NOTIFYICONDATAW nid = MakeNid(m_owner, L"VRC Lyrics");
        Shell_NotifyIconW(NIM_ADD, &nid);
        return true;
    }
    if (msg != kMsgId) return false;

    UINT ev = LOWORD(lp);
    switch (ev) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (OnShow) OnShow();
            return true;
        case WM_RBUTTONUP: {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Show");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, L"Exit");
            // 必须先 SetForegroundWindow,否则 TrackPopupMenu 点击外面不会消失。
            SetForegroundWindow(m_owner);
            int cmd = TrackPopupMenu(menu,
                TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                pt.x, pt.y, 0, m_owner, nullptr);
            DestroyMenu(menu);
            if (cmd == 1 && OnShow) OnShow();
            if (cmd == 2 && OnQuit) OnQuit();
            return true;
        }
    }
    return false;
}

}
