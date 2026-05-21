#include "foreground.h"

#include <Windows.h>
#include <psapi.h>
#include <string>

#pragma comment(lib, "psapi.lib")

namespace util {

static std::string Utf8FromWide(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        r.data(), n, nullptr, nullptr);
    return r;
}

// 常见 exe 友好别名,key 是小写 exe 文件名(去掉 .exe)。空 value 表示屏蔽。
static const char* FriendlyAlias(const std::wstring& stem) {
    struct E { const wchar_t* k; const char* v; };
    static const E table[] = {
        { L"vrchat",          "VRChat"   },
        { L"cloudmusic",      "Netease"  },
        { L"orpheus",         "Netease"  },
        { L"chrome",          "Chrome"   },
        { L"msedge",          "Edge"     },
        { L"firefox",         "Firefox"  },
        { L"code",            "VSCode"   },
        { L"devenv",          "VS"       },
        { L"discord",         "Discord"  },
        { L"steam",           "Steam"    },
        { L"cs2",             "CS2"      },
        { L"dota2",           "Dota 2"   },
        { L"explorer",        ""         },  // suppress
        { L"shellexperiencehost", "" },
        { L"applicationframehost", "" },
        { L"searchhost",      ""         },
        { L"startmenuexperiencehost", "" },
        { L"textinputhost",   ""         },
        { L"taskmgr",         ""         },
        { L"vrc-lyrics",      ""         },  // never report ourselves
    };
    for (auto const& e : table) {
        if (stem == e.k) return e.v;
    }
    return nullptr;
}

std::string ForegroundAppName() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return {};

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};

    wchar_t wpath[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(proc, 0, wpath, &sz);
    CloseHandle(proc);
    if (!ok) return {};

    std::wstring path(wpath, sz);
    auto slash = path.find_last_of(L"\\/");
    std::wstring stem = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    auto dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem.resize(dot);

    std::wstring lower = stem;
    for (auto& c : lower) c = (wchar_t)towlower(c);

    if (const char* alias = FriendlyAlias(lower)) {
        if (*alias == '\0') return {};   // suppressed
        return alias;
    }
    return Utf8FromWide(stem);
}

}
