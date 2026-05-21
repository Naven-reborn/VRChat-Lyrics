#include "lyrics_service.h"
#include "netease.h"
#include <cstdio>
#include <cstdarg>
#include <chrono>

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

void Service::RequestNcm(const std::string& ncm_id) {
    if (ncm_id.empty()) {
        m_current.store(std::shared_ptr<Bundle>{});
        std::lock_guard<std::mutex> lk(m_mu);
        m_last_requested_id.clear();
        m_pending_id.clear();
        return;
    }
    std::lock_guard<std::mutex> lk(m_mu);
    // 关键:用 m_last_requested_id 做端到端去重(pending/in-flight/已发布都算)。
    // 没这一步的话,主循环每帧都调 RequestNcm,worker 永远拿不到稳定 ID,结果
    // 全被 drop —— 之前就是这个 bug 导致歌词必须拖窗口才出来。
    if (m_last_requested_id == ncm_id) return;
    DiagLog("request id=%s (was=%s)", ncm_id.c_str(), m_last_requested_id.c_str());
    m_last_requested_id = ncm_id;
    m_pending_id = ncm_id;
    m_cv.notify_one();
}

void Service::Worker() {
    while (m_running.load()) {
        std::string id;
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [&]{ return !m_running.load() || !m_pending_id.empty(); });
            if (!m_running.load()) return;
            id = m_pending_id;
            m_pending_id.clear();
        }

        DiagLog("fetch start id=%s", id.c_str());
        auto t0 = std::chrono::steady_clock::now();
        auto lines = FetchNeteaseLyrics(id, m_include_translation.load());
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        DiagLog("fetch done id=%s lines=%zu took=%lldms", id.c_str(), lines.size(), (long long)dt);

        // 直接发布,即使切歌了也无所谓 —— 主循环会用 bundle->ncm_id == track->ncm_id
        // 判断旧结果,旧的就当"没歌词"处理。
        auto bundle = std::make_shared<Bundle>();
        bundle->ncm_id = id;
        bundle->lines  = std::move(lines);
        m_current.store(bundle);
        DiagLog("published id=%s has_lyrics=%d", id.c_str(), bundle->has_lyrics() ? 1 : 0);
    }
}

}
