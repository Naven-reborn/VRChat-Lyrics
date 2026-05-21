#include "netease.h"
#include "../net/winhttp_client.h"
#include "json.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdarg>

namespace lyrics {

static void DiagLog(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    FILE* f = nullptr; fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs("[netease] ", f); fputs(msg, f); fputs("\n", f); fclose(f); }
}

std::vector<LrcLine> FetchNeteaseLyrics(const std::string& ncm_id,
                                        bool include_translation) {
    if (ncm_id.empty()) return {};

    std::string url = "https://music.163.com/api/song/lyric?os=pc&id=" + ncm_id +
                      "&lv=-1&kv=-1&tv=-1";
    std::string headers =
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
        "Referer: https://music.163.com/\r\n"
        "Accept: */*\r\n";

    std::string body;
    int status = 0;
    bool ok = net::HttpGet(url, headers, body, status);
    DiagLog("HTTP id=%s status=%d body=%zu ok=%d", ncm_id.c_str(), status, body.size(), ok ? 1 : 0);
    if (body.size() < 1500) {
        // 体积小说明大概率是错误返回 —— 整段 dump 出来排查方便。
        DiagLog("body-dump: %s", body.c_str());
    } else {
        DiagLog("body-head: %.400s", body.c_str());
    }
    if (!ok || body.empty()) return {};

    std::vector<LrcLine> result;
    try {
        auto j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded()) { DiagLog("json parse FAILED"); return {}; }

        bool has_lrc_obj = j.contains("lrc") && j["lrc"].is_object();
        bool has_lyric = has_lrc_obj && j["lrc"].contains("lyric") && j["lrc"]["lyric"].is_string();
        DiagLog("json: lrc-obj=%d lyric-field=%d code=%d",
                has_lrc_obj ? 1 : 0, has_lyric ? 1 : 0,
                j.contains("code") && j["code"].is_number_integer() ? j["code"].get<int>() : -1);

        if (has_lyric) {
            std::string lrc = j["lrc"]["lyric"].get<std::string>();
            DiagLog("lrc bytes=%zu first=%.80s", lrc.size(), lrc.c_str());
            result = ParseLrc(lrc);
            DiagLog("parsed %zu lines", result.size());
        }

        if (include_translation && j.contains("tlyric") && j["tlyric"].is_object() &&
            j["tlyric"].contains("lyric") && j["tlyric"]["lyric"].is_string()) {
            std::string tlrc = j["tlyric"]["lyric"].get<std::string>();
            auto trans = ParseLrc(tlrc);
            for (auto& tl : trans) {
                auto it = std::lower_bound(result.begin(), result.end(), tl.ms,
                    [](const LrcLine& a, int64_t v) { return a.ms < v; });
                if (it != result.end() && it->ms == tl.ms && !tl.text.empty()) {
                    it->text += "\n";
                    it->text += tl.text;
                }
            }
        }
    } catch (std::exception const& e) {
        DiagLog("exception: %s", e.what());
        return {};
    }
    return result;
}

}
