#include "config.h"

#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "json.hpp"

#pragma comment(lib, "Shell32.lib")

namespace config {

static std::filesystem::path ConfigDir() {
    PWSTR appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        return std::filesystem::current_path();
    }
    std::filesystem::path p = appdata;
    CoTaskMemFree(appdata);
    return p / L"vrc-lyrics";
}

static std::filesystem::path ConfigPath() {
    return ConfigDir() / L"config.json";
}

void Load(menu::State& s) {
    auto path = ConfigPath();
    std::ifstream f(path);
    if (!f.is_open()) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }

    auto get_str = [&](const char* k, char* dst, size_t cap) {
        if (!j.contains(k) || !j[k].is_string()) return;
        std::string v = j[k].get<std::string>();
        size_t n = v.size() < cap - 1 ? v.size() : cap - 1;
        std::memcpy(dst, v.data(), n);
        dst[n] = 0;
    };
    auto get_int = [&](const char* k, int& dst) {
        if (j.contains(k) && j[k].is_number_integer()) dst = j[k].get<int>();
    };
    auto get_bool = [&](const char* k, bool& dst) {
        if (j.contains(k) && j[k].is_boolean()) dst = j[k].get<bool>();
    };

    int lang = (int)s.language;
    get_int("language", lang);
    s.language = (i18n::Lang)lang;
    int th = (int)s.theme;
    get_int("theme", th);
    s.theme = (menu::Theme)th;
    get_bool("send_while_paused",  s.send_while_paused);
    get_bool("show_foreground_app",s.show_foreground_app);
    get_str ("osc_host",           s.osc_host, sizeof(s.osc_host));
    get_int ("osc_port",           s.osc_port);
    get_int ("rate_limit_ms",      s.rate_limit_ms);
    get_int ("lyrics_provider",    s.lyrics_provider);
    get_bool("include_translation",s.include_translation);
    get_bool("strip_metadata_tags",s.strip_metadata_tags);
    get_str ("fmt_lyrics",         s.fmt_lyrics,    sizeof(s.fmt_lyrics));
    get_str ("fmt_no_lyrics",      s.fmt_no_lyrics, sizeof(s.fmt_no_lyrics));
    get_str ("fmt_paused",         s.fmt_paused,    sizeof(s.fmt_paused));
}

bool Save(const menu::State& s) {
    auto dir = ConfigDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    nlohmann::json j;
    j["language"]           = (int)s.language;
    j["theme"]              = (int)s.theme;
    j["send_while_paused"]  = s.send_while_paused;
    j["show_foreground_app"]= s.show_foreground_app;
    j["osc_host"]           = s.osc_host;
    j["osc_port"]           = s.osc_port;
    j["rate_limit_ms"]      = s.rate_limit_ms;
    j["lyrics_provider"]    = s.lyrics_provider;
    j["include_translation"]= s.include_translation;
    j["strip_metadata_tags"]= s.strip_metadata_tags;
    j["fmt_lyrics"]         = s.fmt_lyrics;
    j["fmt_no_lyrics"]      = s.fmt_no_lyrics;
    j["fmt_paused"]         = s.fmt_paused;

    auto tmp = ConfigPath();
    tmp += L".tmp";
    {
        std::ofstream out(tmp);
        if (!out.is_open()) return false;
        out << j.dump(2);
    }
    std::filesystem::rename(tmp, ConfigPath(), ec);
    return !ec;
}

}
