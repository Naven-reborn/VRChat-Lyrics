#pragma once
#include <string>

namespace bilibili {

// 错误码用枚举,具体翻译在 menu.cpp 里做,不污染解析层。
enum class ErrorCode {
    None = 0,
    NoBv,             // 没从输入里提到 BV 号
    ShortlinkFailed,  // b23.tv 短链没拿到 Location
    Network,          // HTTP 失败 / 超时 / DNS
    Api,              // bilibili API 返回 code != 0(可能是地区限制 / 需要登录)
    NoStream,         // playurl 没有可用的 dash.video / durl
    JsonInvalid,      // JSON 解析失败
};

struct ParseResult {
    bool        ok       = false;
    ErrorCode   error    = ErrorCode::None;
    std::string bvid;       // 解析出来的 BV 号
    std::string title;      // utf-8 视频标题
    std::string url;        // 最终可在 VRChat 里播放的直链
    std::string format;     // "DASH" / "FLV"
    std::string quality;    // 实际选中的质量,如 "1440P" / "1080P"
    std::string node;       // 使用的 CDN 节点域名
};

// 同步解析。input 可以是裸 BV 号 / 完整 bilibili.com URL / b23.tv 短链。
// 函数会自己处理重定向和 API 调用,失败时 ok=false 并填 error。
ParseResult Parse(const std::string& input);

}
