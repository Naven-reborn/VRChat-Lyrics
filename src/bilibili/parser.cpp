#include "parser.h"
#include "../net/winhttp_client.h"
#include "json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <regex>
#include <string>

namespace bilibili {

static void DiagLog(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    FILE* f = nullptr; fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs("[bili] ", f); fputs(msg, f); fputs("\n", f); fclose(f); }
}

// 浏览器风格的请求头 —— bilibili API 对 Referer 和 UA 都比较挑剔,
// 缺一个就会 412 或 -403。
static const char* kCommonHeaders =
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
    "Referer: https://www.bilibili.com/\r\n"
    "Accept: application/json, text/plain, */*\r\n"
    "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n";

// 上游 wangure0329 用了 9 个主节点 + 1 个 mirror 节点轮询,我们这里没有
// "成功率统计" 的运行时,直接固定到深圳 GOSS —— 上游测下来三大节点都能用,
// 这个是默认排第一的,对国内用户最稳。后面如果有失败上报需求再加权重。
static const char* kPreferredNode = "upos-sz-estgoss.bilivideo.com";

// 从任意字符串里抠 BV 号。BV + 后面 10 位 [a-zA-Z0-9]。
static std::string ExtractBv(const std::string& s) {
    for (size_t i = 0; i + 12 <= s.size(); ++i) {
        if (s[i] == 'B' && s[i+1] == 'V') {
            bool all_alnum = true;
            for (size_t k = 2; k < 12; ++k) {
                unsigned char c = (unsigned char)s[i + k];
                if (!std::isalnum(c)) { all_alnum = false; break; }
            }
            if (all_alnum) return s.substr(i, 12);
        }
    }
    return {};
}

// 用正则把 URL 里所有 upos-{sz,bj,hz}-xxx.bilivideo.com / .akamaized.net /
// .cloudfront.net 全换成首选节点。能跑就行,不追求极致正则。
static std::string ReplaceCdnHost(const std::string& url, const std::string& node) {
    static const std::regex re(
        R"(upos-(?:sz|bj|hz)-[^/]+?\.(?:bilivideo\.com|akamaized\.net|cloudfront\.net))",
        std::regex::ECMAScript | std::regex::icase);
    return std::regex_replace(url, re, node);
}

// b23.tv/xxx → 完整 bilibili.com URL。利用 net::ResolveRedirect 拿 Location 头,
// 不下载目标页面 HTML(VS 那边以前下载整页太慢)。
static std::string ResolveShortLink(const std::string& url) {
    std::string normalized = url;
    if (normalized.rfind("http://", 0) != 0 && normalized.rfind("https://", 0) != 0) {
        normalized = "https://" + normalized;
    }
    std::string location;
    int status = 0;
    bool ok = net::ResolveRedirect(normalized, kCommonHeaders, location, status);
    DiagLog("shortlink %s -> status=%d loc=%s",
            normalized.c_str(), status, location.c_str());
    if (ok && status >= 300 && status < 400 && !location.empty()) return location;
    return {};
}

// 输入预处理:抠 BV。返回空串表示拿不到。
static std::string NormalizeToBv(const std::string& input_in) {
    // 去掉首尾空白,VRChat 玩家从聊天框复制经常带空格/换行。
    std::string input = input_in;
    while (!input.empty() && (input.front() == ' ' || input.front() == '\t' ||
                              input.front() == '\r' || input.front() == '\n'))
        input.erase(input.begin());
    while (!input.empty() && (input.back() == ' ' || input.back() == '\t' ||
                              input.back() == '\r' || input.back() == '\n'))
        input.pop_back();
    if (input.empty()) return {};

    // 先看裸文本里有没有 BV(覆盖 "BV1..." / "...video/BV1..." / 包含 BV 的任意串)。
    {
        std::string bv = ExtractBv(input);
        if (!bv.empty()) return bv;
    }

    // 短链:走重定向。
    if (input.find("b23.tv/") != std::string::npos) {
        std::string resolved = ResolveShortLink(input);
        if (!resolved.empty()) {
            std::string bv = ExtractBv(resolved);
            if (!bv.empty()) return bv;
        }
    }
    return {};
}

ParseResult Parse(const std::string& input) {
    ParseResult r;

    std::string bvid = NormalizeToBv(input);
    if (bvid.empty()) {
        r.error = ErrorCode::NoBv;
        DiagLog("parse: no BV in input='%s'", input.c_str());
        return r;
    }
    r.bvid = bvid;
    DiagLog("parse: bv=%s", bvid.c_str());

    // Step 1: web-interface/view → cid + title
    int64_t cid = 0;
    {
        std::string url = "https://api.bilibili.com/x/web-interface/view?bvid=" + bvid;
        std::string body;
        int status = 0;
        if (!net::HttpGet(url, kCommonHeaders, body, status)) {
            DiagLog("view http fail status=%d size=%zu", status, body.size());
            r.error = ErrorCode::Network;
            return r;
        }
        try {
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_discarded()) { r.error = ErrorCode::JsonInvalid; return r; }
            int code = j.value("code", -1);
            if (code != 0) {
                DiagLog("view api code=%d", code);
                r.error = ErrorCode::Api;
                return r;
            }
            const auto& data = j["data"];
            if (data.contains("cid") && data["cid"].is_number_integer())
                cid = data["cid"].get<int64_t>();
            if (data.contains("title") && data["title"].is_string())
                r.title = data["title"].get<std::string>();
        } catch (...) {
            r.error = ErrorCode::JsonInvalid;
            return r;
        }
        if (cid == 0) { r.error = ErrorCode::Api; return r; }
    }

    // Step 2: player/playurl → dash.video / durl
    // qn=112 要 2K,fnval=16 = DASH,platform=html5 用来绕开 web 端的防盗链限制。
    {
        char url_buf[256];
        std::snprintf(url_buf, sizeof(url_buf),
            "https://api.bilibili.com/x/player/playurl?"
            "bvid=%s&cid=%lld&qn=112&fnval=16&platform=html5",
            bvid.c_str(), (long long)cid);

        std::string body;
        int status = 0;
        if (!net::HttpGet(url_buf, kCommonHeaders, body, status)) {
            DiagLog("playurl http fail status=%d size=%zu", status, body.size());
            r.error = ErrorCode::Network;
            return r;
        }
        try {
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_discarded()) { r.error = ErrorCode::JsonInvalid; return r; }
            int code = j.value("code", -1);
            if (code != 0) {
                DiagLog("playurl api code=%d", code);
                r.error = ErrorCode::Api;
                return r;
            }
            const auto& data = j["data"];

            // 优先 DASH 视频流。bilibili 给的数组是按质量从高到低排,
            // 我们尽量挑 id==112(1440P),否则取数组第一个(自动最高可用)。
            if (data.contains("dash") && data["dash"].is_object() &&
                data["dash"].contains("video") && data["dash"]["video"].is_array() &&
                !data["dash"]["video"].empty()) {
                const auto& videos = data["dash"]["video"];
                const nlohmann::json* picked = nullptr;
                for (auto& v : videos) {
                    if (v.value("id", 0) == 112) { picked = &v; break; }
                }
                if (!picked) picked = &videos[0];
                if (picked->contains("baseUrl") && (*picked)["baseUrl"].is_string()) {
                    std::string original = (*picked)["baseUrl"].get<std::string>();
                    r.url     = ReplaceCdnHost(original, kPreferredNode);
                    r.format  = "DASH";
                    int qn    = picked->value("id", 0);
                    r.quality = (qn == 120 ? "4K" : qn == 116 ? "1080P60" :
                                 qn == 112 ? "1440P" : qn == 80  ? "1080P" :
                                 qn == 64  ? "720P"  : qn == 32  ? "480P"  :
                                 qn == 16  ? "360P"  : "AUTO");
                    r.node    = kPreferredNode;
                    r.ok      = true;
                    DiagLog("dash picked qn=%d url=%.120s", qn, r.url.c_str());
                    return r;
                }
            }

            // 退化到 durl(FLV),老视频和部分番剧才有。
            if (data.contains("durl") && data["durl"].is_array() &&
                !data["durl"].empty() &&
                data["durl"][0].contains("url") &&
                data["durl"][0]["url"].is_string()) {
                std::string original = data["durl"][0]["url"].get<std::string>();
                r.url     = ReplaceCdnHost(original, kPreferredNode);
                r.format  = "FLV";
                r.quality = "1440P";
                r.node    = kPreferredNode;
                r.ok      = true;
                DiagLog("durl picked url=%.120s", r.url.c_str());
                return r;
            }

            r.error = ErrorCode::NoStream;
            DiagLog("no dash & no durl in playurl response");
            return r;
        } catch (...) {
            r.error = ErrorCode::JsonInvalid;
            return r;
        }
    }
}

}
