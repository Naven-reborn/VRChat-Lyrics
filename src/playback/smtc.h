#pragma once
#include "track.h"
#include <atomic>
#include <memory>
#include <thread>

namespace playback {

class SmtcWatcher {
public:
    bool Start();
    void Stop();

    // Atomic snapshot. May be nullptr if nothing playing yet.
    std::shared_ptr<Track> Current() const {
        return m_current.load();
    }

private:
    void ThreadProc();

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<std::shared_ptr<Track>> m_current;

    // 跨轮询缓存:浏览器 YT Music 的 Position 经常十几秒卡死不动。
    // 我们在 worker 里自己做"粘滞外推",只在真正前进 / seek 时才采信新 raw。
    std::string m_sticky_key;
    int64_t     m_sticky_raw_pos = 0;      // 上次采信的 raw Position
    int64_t     m_sticky_base_pos = 0;     // 对应的已校正位置
    int64_t     m_sticky_base_at_ms = 0;   // steady 时刻
    int64_t     m_sticky_last_upd_ms = 0;  // 上次看到的 LastUpdatedTime(unix ms)
    float       m_sticky_rate = 1.f;
    bool        m_sticky_playing = false;
};

}
