#include "lyrics_service.h"
#include "netease.h"
#include "lrclib.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace lyrics {

static void DiagLog(const char* fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    FILE* f = nullptr; fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs("[lyrics] ", f); fputs(msg, f); fputs("\n", f); fclose(f); }
}

bool Service::Start() {
    if (m_running.exchange(true)) return false;
    m_thread = std::thread(&Service::Worker, this);
    return true;
}

void Service::Stop() {
    if (!m_running.exchange(false)) return;
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void Service::Request(const Query& q) {
    if (q.match_key.empty()) {
        m_current.store(std::shared_ptr<Bundle>{});
        std::lock_guard<std::mutex> lk(m_mu);
        m_last_requested_key.clear();
        m_pending_valid = false;
        return;
    }
    std::lock_guard<std::mutex> lk(m_mu);
    // m_last_requested_key 做端到端去重(pending / in-flight / 已发布都算)。
    // 没这一步主循环每帧都调 Request,worker 永远拿不到稳定 key,结果全被 drop。
    if (m_last_requested_key == q.match_key) return;
    DiagLog("request key=%s (was=%s)", q.match_key.c_str(), m_last_requested_key.c_str());
    m_last_requested_key = q.match_key;
    m_pending = q;
    m_pending_valid = true;
    m_cv.notify_one();
}

void Service::Worker() {
    while (m_running.load()) {
        Query q;
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [&]{ return !m_running.load() || m_pending_valid; });
            if (!m_running.load()) return;
            q = m_pending;
            m_pending_valid = false;
        }

        DiagLog("fetch start key=%s ncm=%s is_ncm=%d",
                q.match_key.c_str(), q.ncm_id.c_str(), q.is_netease ? 1 : 0);
        auto t0 = std::chrono::steady_clock::now();

        std::vector<LrcLine> lines;
        Provider prov = (Provider)m_provider.load();
        bool include_trans = m_include_translation.load();

        auto try_netease = [&]() {
            if (q.is_netease && !q.ncm_id.empty()) {
                lines = FetchNeteaseLyrics(q.ncm_id, include_trans);
            }
        };
        auto try_lrclib = [&]() {
            lines = FetchLrclibLyrics(q.title, q.artist, q.album, q.duration_sec);
        };

        switch (prov) {
            case Provider::NeteaseOnly:
                try_netease();
                break;
            case Provider::NeteaseThenLrclib:
                try_netease();
                if (lines.empty()) try_lrclib();
                break;
            case Provider::LrclibOnly:
                try_lrclib();
                break;
        }

        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        DiagLog("fetch done key=%s lines=%zu took=%lldms",
                q.match_key.c_str(), lines.size(), (long long)dt);

        // 直接发布,即使切歌了也无所谓 —— 主循环用 bundle->match_key == track->match_key
        // 判断旧结果,旧的就当"没歌词"处理。
        auto bundle = std::make_shared<Bundle>();
        bundle->match_key = q.match_key;
        bundle->lines     = std::move(lines);
        m_current.store(bundle);
        DiagLog("published key=%s has_lyrics=%d",
                q.match_key.c_str(), bundle->has_lyrics() ? 1 : 0);
    }
}

}
