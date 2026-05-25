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
    int64_t  position_sampled_at_ms = 0;
    Status   status = Status::Stopped;
    float    playback_rate = 1.f;
    std::vector<uint8_t> thumbnail_bytes; // raw JPG/PNG from SMTC; empty if none

    int64_t EffectivePositionMs(int64_t now_ms) const {
        if (status != Status::Playing) return position_ms;
        return position_ms + (int64_t)((now_ms - position_sampled_at_ms) * playback_rate);
    }
};

}
