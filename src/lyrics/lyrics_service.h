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

struct Bundle {
    std::string          ncm_id;
    std::vector<LrcLine> lines;
    bool                 has_lyrics() const { return !lines.empty(); }
};

class Service {
public:
    bool Start();
    void Stop();

    // Call from any thread. Async-fetches when ncm_id changes; cheap if same.
    void RequestNcm(const std::string& ncm_id);

    void SetIncludeTranslation(bool v) { m_include_translation = v; }

    // Atomic snapshot. May be nullptr if no lyrics yet.
    std::shared_ptr<Bundle> Current() const { return m_current.load(); }

private:
    void Worker();

    std::thread             m_thread;
    std::atomic<bool>       m_running{false};
    std::mutex              m_mu;
    std::condition_variable m_cv;
    std::string             m_pending_id;
    std::string             m_last_requested_id;  // dedupe across the request fence
    std::atomic<bool>       m_include_translation{false};

    std::atomic<std::shared_ptr<Bundle>> m_current;
};

}
