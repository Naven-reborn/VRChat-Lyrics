#include "app_icon.h"

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace host {

// Extract a sized icon from our own EXE.
// PrivateExtractIcons is more reliable for taskbar/tray than LoadImage on a
// multi-size RT_GROUP_ICON — LoadImage sometimes fails to resolve the group
// when the process was already running under an older image / shell cache.
static HICON ExtractFromSelf(int cx, int cy) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH) || !path[0])
        return nullptr;

    HICON icon = nullptr;
    UINT n = PrivateExtractIconsW(path, 0, cx, cy, &icon, nullptr, 1, 0);
    if (n == 0 || n == (UINT)-1 || !icon)
        return nullptr;
    return icon;
}

static HICON LoadSized(int cx, int cy) {
    // 1) Preferred: extract from the running EXE path (works even when resource
    //    load by ordinal is flaky, and always picks the closest size).
    if (HICON icon = ExtractFromSelf(cx, cy))
        return icon;

    // 2) Fallback: LoadImage from the module resource table.
    HMODULE mod = GetModuleHandleW(nullptr);
    if (HICON icon = (HICON)LoadImageW(
            mod, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            cx, cy, LR_DEFAULTCOLOR)) {
        return icon;
    }
    if (HICON icon = (HICON)LoadImageW(
            mod, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE)) {
        return icon;
    }

    // 3) Last resort.
    return LoadIconW(nullptr, IDI_APPLICATION);
}

HICON LoadAppIcon(bool big) {
    int cx = big ? GetSystemMetrics(SM_CXICON) : GetSystemMetrics(SM_CXSMICON);
    int cy = big ? GetSystemMetrics(SM_CYICON) : GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = big ? 32 : 16;
    if (cy <= 0) cy = big ? 32 : 16;

    // On high-DPI taskbars SM_CXICON can be 48/64 — extract that size too.
    return LoadSized(cx, cy);
}

void ApplyAppIcon(HWND hwnd) {
    if (!hwnd) return;

    HICON big = LoadAppIcon(true);
    HICON sm  = LoadAppIcon(false);
    if (!big && !sm) return;

    // HWND icons — what the taskbar / Alt-Tab actually display.
    if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
    if (sm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)sm);

    // Class icons — some shell paths fall back here.
    if (big) SetClassLongPtrW(hwnd, GCLP_HICON,   (LONG_PTR)big);
    if (sm)  SetClassLongPtrW(hwnd, GCLP_HICONSM, (LONG_PTR)sm);

    // Force the non-client frame (and thus the taskbar button) to re-query.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED |
                 SWP_NOACTIVATE);
}

}  // namespace host
