#include "foreground.h"

#include <Windows.h>
#include <psapi.h>
#include <cwctype>
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

// exe 文件名(小写,无扩展名) → (友好别名, 分类)。
// 空别名 = 屏蔽(返回空 ForegroundInfo)。AppCategory::Unknown 让 UI 走"未分类"配色。
struct AliasEntry {
    const wchar_t* stem;
    const char*    alias;
    AppCategory    cat;
};

static const AliasEntry kAliasTable[] = {
    // VR & 游戏
    { L"vrchat",                    "VRChat",     AppCategory::Game },
    { L"steam",                     "Steam",      AppCategory::Game },
    { L"steamwebhelper",            "Steam",      AppCategory::Game },
    { L"epicgameslauncher",         "Epic",       AppCategory::Game },
    { L"battle.net",                "Battle.net", AppCategory::Game },
    { L"cs2",                       "CS2",        AppCategory::Game },
    { L"dota2",                     "Dota 2",     AppCategory::Game },
    { L"genshinimpact",             "Genshin",    AppCategory::Game },
    { L"yuanshen",                  "Genshin",    AppCategory::Game },
    { L"starrail",                  "Star Rail",  AppCategory::Game },
    { L"zenlesszonezero",           "ZZZ",        AppCategory::Game },
    { L"valorant-win64-shipping",   "Valorant",   AppCategory::Game },
    { L"leagueclient",              "LoL",        AppCategory::Game },
    { L"league of legends",         "LoL",        AppCategory::Game },
    { L"r5apex",                    "Apex",       AppCategory::Game },
    { L"factorio",                  "Factorio",   AppCategory::Game },
    { L"minecraft.windows",         "Minecraft",  AppCategory::Game },
    { L"javaw",                     "Java",       AppCategory::Game },

    // 音乐
    { L"cloudmusic",                "Netease",    AppCategory::Music },
    { L"orpheus",                   "Netease",    AppCategory::Music },
    { L"spotify",                   "Spotify",    AppCategory::Music },
    { L"qqmusic",                   "QQ Music",   AppCategory::Music },
    { L"kugou",                     "Kugou",      AppCategory::Music },
    { L"foobar2000",                "foobar2000", AppCategory::Music },

    // 浏览器
    { L"chrome",                    "Chrome",     AppCategory::Browser },
    { L"msedge",                    "Edge",       AppCategory::Browser },
    { L"firefox",                   "Firefox",    AppCategory::Browser },
    { L"opera",                     "Opera",      AppCategory::Browser },
    { L"brave",                     "Brave",      AppCategory::Browser },
    { L"arc",                       "Arc",        AppCategory::Browser },

    // 聊天 / 社交
    { L"discord",                   "Discord",    AppCategory::Chat },
    { L"discordcanary",             "Discord",    AppCategory::Chat },
    { L"discordptb",                "Discord",    AppCategory::Chat },
    { L"qq",                        "QQ",         AppCategory::Chat },
    { L"qqnt",                      "QQ",         AppCategory::Chat },
    { L"wechat",                    "WeChat",     AppCategory::Chat },
    { L"weixin",                    "WeChat",     AppCategory::Chat },
    { L"telegram",                  "Telegram",   AppCategory::Chat },
    { L"slack",                     "Slack",      AppCategory::Chat },
    { L"feishu",                    "Feishu",     AppCategory::Chat },
    { L"larksuite",                 "Lark",       AppCategory::Chat },
    { L"dingtalk",                  "DingTalk",   AppCategory::Chat },

    // 开发
    { L"code",                      "VSCode",     AppCategory::Dev },
    { L"code - insiders",           "VSCode",     AppCategory::Dev },
    { L"cursor",                    "Cursor",     AppCategory::Dev },
    { L"devenv",                    "VS",         AppCategory::Dev },
    { L"rider64",                   "Rider",      AppCategory::Dev },
    { L"idea64",                    "IntelliJ",   AppCategory::Dev },
    { L"pycharm64",                 "PyCharm",    AppCategory::Dev },
    { L"clion64",                   "CLion",      AppCategory::Dev },
    { L"windowsterminal",           "Terminal",   AppCategory::Dev },
    { L"wezterm-gui",               "WezTerm",    AppCategory::Dev },
    { L"unity",                     "Unity",      AppCategory::Dev },
    { L"unrealeditor",              "Unreal",     AppCategory::Dev },
    { L"godot",                     "Godot",      AppCategory::Dev },
    { L"blender",                   "Blender",    AppCategory::Dev },

    // Office / 文档
    { L"winword",                   "Word",       AppCategory::Office },
    { L"excel",                     "Excel",      AppCategory::Office },
    { L"powerpnt",                  "PowerPoint", AppCategory::Office },
    { L"onenote",                   "OneNote",    AppCategory::Office },
    { L"notion",                    "Notion",     AppCategory::Office },
    { L"obsidian",                  "Obsidian",   AppCategory::Office },
    { L"acrord32",                  "PDF",        AppCategory::Office },
    { L"sumatrapdf",                "SumatraPDF", AppCategory::Office },
    { L"wps",                       "WPS",        AppCategory::Office },
    { L"wpsoffice",                 "WPS",        AppCategory::Office },

    // 创作 / 直播
    { L"obs64",                     "OBS",        AppCategory::Stream },
    { L"obs32",                     "OBS",        AppCategory::Stream },
    { L"streamlabs obs",            "Streamlabs", AppCategory::Stream },
    { L"photoshop",                 "Photoshop",  AppCategory::Stream },
    { L"illustrator",               "Illustrator",AppCategory::Stream },
    { L"premiere",                  "Premiere",   AppCategory::Stream },
    { L"afterfx",                   "AfterFX",    AppCategory::Stream },
    { L"davinci resolve",           "DaVinci",    AppCategory::Stream },
    { L"figma",                     "Figma",      AppCategory::Stream },
    { L"clip studio paint",         "Clip Studio",AppCategory::Stream },

    // 屏蔽:系统外壳和我们自己
    { L"explorer",                  "",           AppCategory::Unknown },
    { L"shellexperiencehost",       "",           AppCategory::Unknown },
    { L"applicationframehost",      "",           AppCategory::Unknown },
    { L"searchhost",                "",           AppCategory::Unknown },
    { L"startmenuexperiencehost",   "",           AppCategory::Unknown },
    { L"textinputhost",             "",           AppCategory::Unknown },
    { L"taskmgr",                   "",           AppCategory::Unknown },
    { L"lockapp",                   "",           AppCategory::Unknown },
    { L"vrc-lyrics",                "",           AppCategory::Unknown },
    { L"vrc-lyrics-3.0",            "",           AppCategory::Unknown },
    { L"vrc-lyrics-3.1",            "",           AppCategory::Unknown },
};

static const AliasEntry* FindAlias(const std::wstring& stem) {
    for (auto const& e : kAliasTable) {
        if (stem == e.stem) return &e;
    }
    return nullptr;
}

const char* DefaultCategoryEmoji(AppCategory cat) {
    switch (cat) {
        case AppCategory::Game:    return "\xF0\x9F\x8E\xAE"; // 🎮
        case AppCategory::Browser: return "\xF0\x9F\x8C\x90"; // 🌐
        case AppCategory::Chat:    return "\xF0\x9F\x92\xAC"; // 💬
        case AppCategory::Dev:     return "\xF0\x9F\x92\xBB"; // 💻
        case AppCategory::Music:   return "\xF0\x9F\x8E\xB5"; // 🎵
        case AppCategory::Office:  return "\xF0\x9F\x93\x84"; // 📄
        case AppCategory::Stream:  return "\xF0\x9F\x8E\xAC"; // 🎬
        default:                   return "\xF0\x9F\x93\xA6"; // 📦
    }
}

ForegroundInfo ForegroundApp() {
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
    for (auto& c : lower) c = (wchar_t)std::towlower(c);

    if (const AliasEntry* hit = FindAlias(lower)) {
        if (hit->alias[0] == '\0') return {};   // 屏蔽
        ForegroundInfo info;
        info.name     = hit->alias;
        info.category = hit->cat;
        return info;
    }
    // 未识别的进程,直接拿原始 exe 名,分类为 Unknown。
    ForegroundInfo info;
    info.name     = Utf8FromWide(stem);
    info.category = AppCategory::Unknown;
    return info;
}

std::string ForegroundAppName() {
    return ForegroundApp().name;
}

uint32_t IdleSeconds() {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) return 0;
    DWORD now = GetTickCount();
    // 防止 wrap-around 造成负数:tick 50 天回卷,这里钳到 0。
    if (now < lii.dwTime) return 0;
    return (uint32_t)((now - lii.dwTime) / 1000u);
}

}
