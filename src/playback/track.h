#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace playback {

enum class Status { Stopped = 0, Playing = 1, Paused = 2 };

struct Track {
    std::string title;
    std::string artist;
    std::string album;
    std::string ncm_id;       // empty if SMTC source isn't netease
    std::string source_app;   // SourceAppUserModelId — for debugging
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
