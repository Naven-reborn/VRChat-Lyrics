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
};

}
