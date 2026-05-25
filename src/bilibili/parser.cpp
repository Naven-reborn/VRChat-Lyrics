#include "parser.h"
#include "../net/winhttp_client.h"
#include "json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdarg>
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

// v3.2:不再强制 CDN 节点替换。
// 原因:VRChat 的可信视频域名白名单里只有 bilibili.com 网页层,
// upos-*.bilivideo.com / akamaized.net / cloudfront.net 都不在白名单上。
// 强制替换到任何一个 host 都不能让视频在 VRChat 里"无需 Untrusted URLs 即可播放",
// 反而失去了 bilibili 自己根据用户位置选最近 CDN 的能力。
// 直接用 bilibili 给的原始 baseUrl,host 多样化(各种 CDN 都有),命中
// 用户已放行白名单的概率反而比固定一个节点高一点。

// 从 baseUrl 抽 host(不含 scheme 和 path),给 UI 显示当前 CDN host 用。
static std::string ExtractHost(const std::string& url) {
    auto p = url.find("://");
    if (p == std::string::npos) return {};
    auto start = p + 3;
    auto slash = url.find('/', start);
    return url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
}

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

    // Step 2: player/playurl → durl (单文件 MP4 / FLV)
    //
    // v3.3 起改用 fnval=1(MP4 单文件)替代 fnval=16(DASH)。
    // 原因:DASH 给的 baseUrl 扩展名是 .m4s(fragmented MP4 segment),VRChat 的
    // AVPro / Unity VideoPlayer 看到 .m4s 直接报"不支持的链接"。MP4 durl 拿到的
    // URL 是 .mp4 / .flv 单文件,音视频合在一起,所有播放器都吃。代价:画质上限
    // 1080P(qn=80),没法上 4K/HDR/8K —— 但 VRChat 视频墙根本不需要 4K。
    //
    // qn=80 直接要 1080P,API 会自动降到该视频实际可用的最高质量。platform=html5
    // 仍然带上,用来绕开网页端防盗链限制(给的 URL 不需要带 Referer 也能拉)。
    {
        char url_buf[256];
        std::snprintf(url_buf, sizeof(url_buf),
            "https://api.bilibili.com/x/player/playurl?"
            "bvid=%s&cid=%lld&qn=80&fnval=1&platform=html5",
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

            // 主路径:durl 单文件。data.quality 是实际返回的 qn(可能比请求的低)。
            if (data.contains("durl") && data["durl"].is_array() &&
                !data["durl"].empty() &&
                data["durl"][0].contains("url") &&
                data["durl"][0]["url"].is_string()) {
                r.url     = data["durl"][0]["url"].get<std::string>();
                int qn    = data.value("quality", 0);
                r.quality = (qn == 80 ? "1080P" : qn == 64 ? "720P" :
                             qn == 32 ? "480P"  : qn == 16 ? "360P" : "AUTO");
                // format 看 URL 后缀(去掉 query string 再判),.mp4 / .flv 都常见。
                r.format  = "MP4";
                {
                    std::string p = r.url;
                    auto qpos = p.find('?');
                    if (qpos != std::string::npos) p.resize(qpos);
                    if (p.size() >= 4 && p.compare(p.size()-4, 4, ".flv") == 0) r.format = "FLV";
                }
                r.node    = ExtractHost(r.url);
                r.ok      = true;
                DiagLog("durl picked qn=%d fmt=%s host=%s",
                        qn, r.format.c_str(), r.node.c_str());
                return r;
            }

            // 兜底:个别新视频可能只给 DASH(理论上 fnval=1 不会触发这里,但 API
            // 偶尔有边缘 case)。给出来用户能复制,只是 VRChat 大概率不认 .m4s。
            if (data.contains("dash") && data["dash"].is_object() &&
                data["dash"].contains("video") && data["dash"]["video"].is_array() &&
                !data["dash"]["video"].empty()) {
                const auto& videos = data["dash"]["video"];
                if (videos[0].contains("baseUrl") && videos[0]["baseUrl"].is_string()) {
                    r.url     = videos[0]["baseUrl"].get<std::string>();
                    r.format  = "DASH";
                    int qn    = videos[0].value("id", 0);
                    r.quality = (qn == 120 ? "4K" : qn == 116 ? "1080P60" :
                                 qn == 112 ? "1440P" : qn == 80  ? "1080P" :
                                 qn == 64  ? "720P"  : qn == 32  ? "480P"  :
                                 qn == 16  ? "360P"  : "AUTO");
                    r.node    = ExtractHost(r.url);
                    r.ok      = true;
                    DiagLog("dash fallback qn=%d host=%s", qn, r.node.c_str());
                    return r;
                }
            }

            r.error = ErrorCode::NoStream;
            DiagLog("no durl & no dash in playurl response");
            return r;
        } catch (...) {
            r.error = ErrorCode::JsonInvalid;
            return r;
        }
    }
}

}
