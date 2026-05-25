#include "lrclib.h"
#include "../net/winhttp_client.h"
#include "json.hpp"

#include <cstdarg>
#include <cstdio>

namespace lyrics {

static void DiagLog(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    FILE* f = nullptr; fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs("[lrclib] ", f); fputs(msg, f); fputs("\n", f); fclose(f); }
}

// URL-encode RFC 3986 unreserved 之外的全部字节(UTF-8 直接走 %xx 转义)。
// 不做 form-style 的空格 → '+' 替换 —— LRCLib 接受 %20。
static std::string UrlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        bool unreserved = (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::vector<LrcLine> FetchLrclibLyrics(const std::string& title,
                                       const std::string& artist,
                                       const std::string& album,
                                       int duration_sec) {
    if (title.empty() && artist.empty()) {
        DiagLog("skip: title+artist both empty");
        return {};
    }

    // /api/get 是精确匹配端点 —— 全字段都要对得上。匹配率不够再做 /api/search 兜底。
    std::string url = "https://lrclib.net/api/get?track_name=" + UrlEncode(title) +
                      "&artist_name=" + UrlEncode(artist);
    if (!album.empty())     url += "&album_name=" + UrlEncode(album);
    if (duration_sec > 0) {
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", duration_sec);
        url += "&duration=";
        url += buf;
    }

    std::string headers =
        "User-Agent: vrc-lyrics-cxx/3.2 (https://github.com/your/repo)\r\n"
        "Accept: application/json\r\n";

    std::string body;
    int status = 0;
    bool ok = net::HttpGet(url, headers, body, status);
    DiagLog("get status=%d body=%zu title=%.60s artist=%.60s",
            status, body.size(), title.c_str(), artist.c_str());

    if (status == 404 || !ok) {
        // 精确匹配没结果时再试 /api/search 拿头条 —— 用户改了元数据 / 翻唱版本会触发这条。
        std::string s_url = "https://lrclib.net/api/search?track_name=" + UrlEncode(title);
        if (!artist.empty()) s_url += "&artist_name=" + UrlEncode(artist);
        std::string s_body;
        int s_status = 0;
        bool s_ok = net::HttpGet(s_url, headers, s_body, s_status);
        DiagLog("search status=%d body=%zu", s_status, s_body.size());
        if (!s_ok || s_body.empty()) return {};
        try {
            auto arr = nlohmann::json::parse(s_body, nullptr, false);
            if (arr.is_discarded() || !arr.is_array() || arr.empty()) return {};
            // 头条往往是最好匹配 —— LRCLib 自己有相关度排序。
            const auto& first = arr[0];
            if (!first.contains("syncedLyrics") || !first["syncedLyrics"].is_string()) {
                DiagLog("search top hit has no syncedLyrics");
                return {};
            }
            std::string lrc = first["syncedLyrics"].get<std::string>();
            auto lines = ParseLrc(lrc);
            DiagLog("search parsed %zu lines", lines.size());
            return lines;
        } catch (...) {
            return {};
        }
    }

    try {
        auto j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded()) { DiagLog("json parse FAILED"); return {}; }
        if (!j.contains("syncedLyrics") || !j["syncedLyrics"].is_string()) {
            DiagLog("get response missing syncedLyrics — instrumental?");
            return {};
        }
        std::string lrc = j["syncedLyrics"].get<std::string>();
        auto lines = ParseLrc(lrc);
        DiagLog("get parsed %zu lines", lines.size());
        return lines;
    } catch (std::exception const& e) {
        DiagLog("exception: %s", e.what());
        return {};
    }
}

}
