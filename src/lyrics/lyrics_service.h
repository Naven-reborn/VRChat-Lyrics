#pragma once
#include "lrc_parser.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lyrics {

// 提供者优先级,跟 menu.h::State::lyrics_provider 一一对应。
enum class Provider {
    NeteaseOnly        = 0,
    NeteaseThenLrclib  = 1,
    LrclibOnly         = 2,
};

// Worker 收到的查询请求。NetEase 走 ncm_id 直链;其它源(Spotify/YTMusic/Other)
// 走 title+artist+album+duration 查 LRCLib。match_key 由调用方填,用来跨帧去重。
struct Query {
    std::string match_key;   // 跨帧去重的稳定 key,空表示"清空当前歌词"
    std::string ncm_id;      // 仅 NetEase 有
    std::string title;
    std::string artist;
    std::string album;
    int         duration_sec = 0;
    bool        is_netease = false;  // 主线程直接告诉 worker, worker 不再猜
};

struct Bundle {
    std::string          match_key;
    std::vector<LrcLine> lines;
    bool                 has_lyrics() const { return !lines.empty(); }
};

class Service {
public:
    bool Start();
    void Stop();

    // 任何线程都可调。match_key 相同会去重。
    void Request(const Query& q);

    void SetIncludeTranslation(bool v) { m_include_translation = v; }
    void SetProvider(Provider p)       { m_provider = (int)p; }

    // Atomic snapshot. May be nullptr if no lyrics yet.
    std::shared_ptr<Bundle> Current() const { return m_current.load(); }

private:
    void Worker();

    std::thread             m_thread;
    std::atomic<bool>       m_running{false};
    std::mutex              m_mu;
    std::condition_variable m_cv;
    Query                   m_pending;       // 由 m_pending_valid 决定是否有效
    bool                    m_pending_valid = false;
    std::string             m_last_requested_key;  // dedupe across the request fence
    std::atomic<bool>       m_include_translation{false};
    std::atomic<int>        m_provider{(int)Provider::NeteaseThenLrclib};

    std::atomic<std::shared_ptr<Bundle>> m_current;
};

}
