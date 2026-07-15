#pragma once
#include <Windows.h>

// Shared application icon (from assets/app.ico embedded via app.rc).
// Resource ID must match app.rc (IDI_APPICON = 1).
#ifndef IDI_APPICON
#define IDI_APPICON 1
#endif

namespace host {

// Load the embedded app icon. big=true → 32x32 (window/taskbar),
// big=false → 16x16 (tray / small). Caller does NOT destroy the handle —
// LoadImage with LR_SHARED is used so Windows owns the lifetime.
// Falls back to IDI_APPLICATION if the resource is missing.
HICON LoadAppIcon(bool big);

// Convenience: set both big and small icons on an HWND (taskbar + title).
void ApplyAppIcon(HWND hwnd);

}  // namespace host
