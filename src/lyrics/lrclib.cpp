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
        // 精确匹配没结果时再试 /api/search。
        // YT Music 网页端元数据经常脏(Title 里塞 Artist、duration 偏差),
        // 所以 search 不只取头条:在前几条里挑有 syncedLyrics、时长最接近的。
        auto try_search = [&](const std::string& q_title, const std::string& q_artist) -> std::vector<LrcLine> {
            if (q_title.empty()) return {};
            std::string s_url = "https://lrclib.net/api/search?track_name=" + UrlEncode(q_title);
            if (!q_artist.empty()) s_url += "&artist_name=" + UrlEncode(q_artist);
            std::string s_body;
            int s_status = 0;
            bool s_ok = net::HttpGet(s_url, headers, s_body, s_status);
            DiagLog("search status=%d body=%zu q_title=%.60s q_artist=%.40s",
                    s_status, s_body.size(), q_title.c_str(), q_artist.c_str());
            if (!s_ok || s_body.empty()) return {};
            try {
                auto arr = nlohmann::json::parse(s_body, nullptr, false);
                if (arr.is_discarded() || !arr.is_array() || arr.empty()) return {};

                int best_i = -1;
                int best_score = -1;
                int n = (int)arr.size();
                if (n > 8) n = 8; // 只看前 8 条,够用
                for (int i = 0; i < n; ++i) {
                    const auto& hit = arr[i];
                    if (!hit.contains("syncedLyrics") || !hit["syncedLyrics"].is_string()) continue;
                    if (hit["syncedLyrics"].get_ref<const std::string&>().empty()) continue;
                    int score = 100 - i * 5; // 相关度排序靠前加分
                    if (duration_sec > 0 && hit.contains("duration") && hit["duration"].is_number()) {
                        int d = 0;
                        if (hit["duration"].is_number_integer()) d = hit["duration"].get<int>();
                        else d = (int)hit["duration"].get<double>();
                        int dd = d - duration_sec;
                        if (dd < 0) dd = -dd;
                        // ±2s 内满分,超过逐步扣
                        if (dd <= 2) score += 40;
                        else if (dd <= 5) score += 20;
                        else if (dd <= 10) score += 5;
                        else score -= 10;
                    }
                    if (score > best_score) { best_score = score; best_i = i; }
                }
                if (best_i < 0) {
                    DiagLog("search: no hit with syncedLyrics in top %d", n);
                    return {};
                }
                std::string lrc = arr[best_i]["syncedLyrics"].get<std::string>();
                auto lines = ParseLrc(lrc);
                DiagLog("search picked #%d score=%d lines=%zu", best_i, best_score, lines.size());
                return lines;
            } catch (...) {
                return {};
            }
        };

        auto lines = try_search(title, artist);
        // 再兜底:artist 为空 / 匹配失败时,只靠 title 搜一次
        if (lines.empty() && !artist.empty()) {
            lines = try_search(title, {});
        }
        return lines;
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
