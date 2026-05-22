#pragma once
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

namespace audio {

struct RelayConfig {
    std::wstring target_device_id; // empty = default endpoint
    float        gain_db = 0.f;
    bool         limiter = true;
};

struct RelayStatus {
    bool        running       = false;
    bool        capturing     = false;
    bool        rendering     = false;
    uint32_t    capture_sr    = 0;
    uint32_t    render_sr     = 0;
    float       peak_dbfs     = -120.f;
    std::string status_text;   // human-readable, displayed in UI
    std::string error_text;    // non-empty on hard failure
};

// Lifecycle: Start() spawns a worker thread that handles COM init,
// process discovery, capture+render open, and the audio loop until Stop()
// signals the stop event. Status is published atomically via a small struct.
class Relay {
public:
    Relay() = default;
    ~Relay() { Stop(); }

    // Start does NOT block on the loop — it kicks off the worker and returns.
    // GetStatus() will reflect whether the worker actually came online.
    bool Start(const RelayConfig& cfg);

    void Stop();

    void SetGainDb(float db) { gain_db_.store(db); }
    void SetLimiter(bool on) { limiter_.store(on); }

    // Live runtime tweaks: device change requires Stop+Start.
    RelayStatus GetStatus() const;

private:
    void WorkerEntry(RelayConfig cfg);

    std::thread        worker_;
    std::atomic<bool>  worker_running_{ false };
    HANDLE             stop_event_ = nullptr;

    std::atomic<float> gain_db_{ 0.f };
    std::atomic<bool>  limiter_{ true };

    mutable std::mutex status_mtx_;
    RelayStatus        status_;
};

} // namespace audio
