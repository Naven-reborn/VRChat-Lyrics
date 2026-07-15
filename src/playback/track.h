#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace playback {

enum class Status { Stopped = 0, Playing = 1, Paused = 2 };

// 识别音乐源:决定歌词怎么查。NCM 有 ID 走网易云直链;Spotify / YTMusic 没 ID,
// 只能靠 title+artist+album+duration 去 LRCLib 模糊匹配。
enum class Source { Other = 0, NetEase = 1, Spotify = 2, YTMusic = 3 };

struct Track {
    std::string title;
    std::string artist;
    std::string album;
    std::string ncm_id;       // 仅 Source::NetEase 时有意义,其它源恒为空
    std::string source_app;   // SourceAppUserModelId — for debugging
    Source      source = Source::Other;

    // 给 lyrics service 做去重 / 新鲜度判定。NCM:"ncm:12345";其它:"meta:title|artist|album|dur"
    // 空表示没有可识别的曲目(SMTC session 拉不到合适字段)。
    std::string match_key;

    int64_t  position_ms = 0;
    int64_t  duration_ms = 0;
    // steady_clock 毫秒时刻:SMTC 报告 position_ms 对应的"有效采样点"。
    // 浏览器 YT Music 的 Timeline.Position 经常十几秒才跳一次,且 LastUpdatedTime
    // 会一起卡死 —— 这时不能靠 SMTC 锚点,必须用本地采样时钟持续外推。
    int64_t  position_sampled_at_ms = 0;
    Status   status = Status::Stopped;
    float    playback_rate = 1.f;
    // 浏览器媒体会话外推更激进:允许更长"无刷新"窗口,避免歌词卡在十几秒前。
    bool     sticky_timeline = false;
    std::vector<uint8_t> thumbnail_bytes; // raw JPG/PNG from SMTC; empty if none

    int64_t EffectivePositionMs(int64_t now_ms) const {
        if (status != Status::Playing) return position_ms;
        int64_t elapsed = now_ms - position_sampled_at_ms;
        if (elapsed < 0) elapsed = 0;

        // 普通源 15s;浏览器 YT Music / 粘滞时间线源拉到 3 分钟。
        // 超过上限后冻结在"最后可靠位置 + 上限",而不是突然跳回 raw position。
        const int64_t max_extrap = sticky_timeline ? 180000 : 15000;
        if (elapsed > max_extrap) elapsed = max_extrap;

        double rate = playback_rate;
        // 浏览器 SMTC 有时给 0 / 垃圾 rate,当 1.0 用。
        if (!(rate > 0.05 && rate < 4.0)) rate = 1.0;

        int64_t pos = position_ms + (int64_t)(elapsed * rate);
        if (duration_ms > 0 && pos > duration_ms) pos = duration_ms;
        if (pos < 0) pos = 0;
        return pos;
    }
};

}
