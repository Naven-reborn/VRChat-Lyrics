#include "relay.h"
#include "process_find.h"
#include "process_loopback.h"
#include "wasapi_render.h"
#include "ring_buffer.h"
#include "limiter.h"

#include <Windows.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <avrt.h>
#include <combaseapi.h>
#include <vector>
#include <chrono>
#include <cstring>

#pragma comment(lib, "avrt.lib")

// Hand-defined to avoid pulling ksmedia.h.
static const GUID kKsDataFormatSubtypeIeeeFloat =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

#ifndef SPEAKER_FRONT_LEFT
#define SPEAKER_FRONT_LEFT  0x1
#define SPEAKER_FRONT_RIGHT 0x2
#endif

namespace audio {

namespace {

constexpr uint32_t kFmtSampleRate = 48000;
constexpr uint16_t kFmtChannels   = 2;

WAVEFORMATEXTENSIBLE MakeFloat32StereoFormat() {
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag       = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels        = kFmtChannels;
    wf.Format.nSamplesPerSec   = kFmtSampleRate;
    wf.Format.wBitsPerSample   = 32;
    wf.Format.nBlockAlign      = kFmtChannels * 4;
    wf.Format.nAvgBytesPerSec  = kFmtSampleRate * wf.Format.nBlockAlign;
    wf.Format.cbSize           = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask           = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wf.SubFormat               = kKsDataFormatSubtypeIeeeFloat;
    return wf;
}

} // namespace

bool Relay::Start(const RelayConfig& cfg) {
    if (worker_running_.load()) return false;

    gain_db_.store(cfg.gain_db);
    limiter_.store(cfg.limiter);

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) return false;

    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_ = {};
        status_.status_text = "Starting...";
    }
    worker_running_.store(true);
    worker_ = std::thread([this, cfg]() { WorkerEntry(cfg); });
    return true;
}

void Relay::Stop() {
    if (!worker_running_.load() && !worker_.joinable()) return;
    if (stop_event_) SetEvent(stop_event_);
    if (worker_.joinable()) worker_.join();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    worker_running_.store(false);
}

RelayStatus Relay::GetStatus() const {
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_;
}

void Relay::WorkerEntry(RelayConfig cfg) {
    auto publish = [&](auto fn) {
        std::lock_guard<std::mutex> lk(status_mtx_);
        fn(status_);
    };
    auto set_text = [&](const std::string& s) {
        publish([&](RelayStatus& st) { st.status_text = s; });
    };
    auto set_error = [&](const std::string& s) {
        publish([&](RelayStatus& st) {
            st.error_text = s;
            st.running = false;
            st.capturing = false;
            st.rendering = false;
        });
    };

    HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_initialized = SUCCEEDED(com_hr);
    if (!com_initialized) {
        set_error("CoInitialize failed");
        worker_running_.store(false);
        return;
    }

    DWORD mm_task_index = 0;
    HANDLE mm_task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mm_task_index);

    auto cleanup = [&]() {
        if (mm_task) AvRevertMmThreadCharacteristics(mm_task);
        if (com_initialized) CoUninitialize();
        worker_running_.store(false);
    };

    auto netease = FindNeteaseRoot();
    if (!netease) {
        set_error("Netease not running");
        cleanup();
        return;
    }

    WAVEFORMATEXTENSIBLE fmt = MakeFloat32StereoFormat();

    ProcessLoopbackCapture cap;
    std::string err;
    if (!cap.Open(netease->root_pid, fmt, err)) {
        set_error("Capture open failed: " + err);
        cleanup();
        return;
    }

    WasapiRender render;
    if (!render.Open(cfg.target_device_id, fmt, err)) {
        set_error("Render open failed: " + err);
        cap.Close();
        cleanup();
        return;
    }

    // Prime render with one buffer of silence so the initial capture-fill gap
    // doesn't underrun.
    render.DrainSilence(render.SampleRate() / 100); // ~10ms

    RingBuffer ring;
    ring.Init(32 * 1024); // ~85ms at 48k stereo float

    SoftLimiter limiter;
    limiter.Configure(-3.0f, limiter_.load());

    // Scratch buffer for one pull (max ~50ms = 2400 frames).
    std::vector<float> scratch(kFmtSampleRate / 10 * kFmtChannels, 0.f); // 100ms headroom

    publish([&](RelayStatus& st) {
        st.running   = true;
        st.capturing = true;
        st.rendering = true;
        st.capture_sr = kFmtSampleRate;
        st.render_sr  = render.SampleRate();
        st.error_text.clear();
        st.status_text = "Running";
    });

    HANDLE waits[3] = { stop_event_, cap.Event(), render.Event() };

    auto last_alive_check = std::chrono::steady_clock::now();
    uint32_t pid = netease->root_pid;

    bool fatal = false;

    while (!fatal) {
        DWORD w = WaitForMultipleObjects(3, waits, FALSE, 200);
        if (w == WAIT_OBJECT_0) break; // stop requested
        // WAIT_TIMEOUT also falls through — we still try to drain.

        // Pull all available capture data into the ring.
        for (;;) {
            bool silent = false;
            bool ok = true;
            uint32_t got = cap.Pull(scratch.data(),
                                    (uint32_t)(scratch.size() / kFmtChannels),
                                    silent, ok, err);
            if (!ok) {
                // Hard error from capture — if Netease died, surface a soft message.
                if (err.find("resources invalidated") != std::string::npos) {
                    set_text("Netease closed, relay stopped");
                } else {
                    set_error("Capture: " + err);
                }
                fatal = true;
                break;
            }
            if (got == 0) break;

            float linear = DbToLinear(gain_db_.load());
            if (linear != 1.f) {
                ApplyGain(scratch.data(), got, kFmtChannels, linear);
            }
            limiter.Configure(-3.0f, limiter_.load());
            limiter.Process(scratch.data(), got, kFmtChannels);

            ring.Write(scratch.data(), (size_t)got * kFmtChannels * sizeof(float));

            publish([&](RelayStatus& st) { st.peak_dbfs = limiter.LastPeakDbFs(); });
        }
        if (fatal) break;

        // Drain ring into render until either ring empty or render full.
        for (;;) {
            uint32_t free_frames = 0;
            // We don't have a direct "GetCurrentPadding" exposed; just try
            // a small chunk and let Push report what it took.
            constexpr uint32_t kChunk = 480; // 10ms @ 48k
            float tmp[kChunk * kFmtChannels];
            size_t want_bytes = sizeof(tmp);
            size_t got_bytes = ring.Read(tmp, want_bytes);
            if (got_bytes == 0) break;
            uint32_t got_frames = (uint32_t)(got_bytes / (kFmtChannels * sizeof(float)));

            bool ok = true;
            uint32_t pushed = render.Push(tmp, got_frames, ok, err);
            if (!ok) {
                set_error("Render: " + err);
                fatal = true;
                break;
            }
            if (pushed < got_frames) {
                // Render buffer full — push back the unwritten tail.
                size_t leftover_bytes = ((size_t)got_frames - pushed) * kFmtChannels * sizeof(float);
                ring.Write(tmp + pushed * kFmtChannels, leftover_bytes);
                break;
            }
            (void)free_frames;
        }

        // Periodically verify the target process is still alive (catches
        // cases where capture doesn't surface RESOURCES_INVALIDATED quickly).
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_alive_check).count() > 1500) {
            last_alive_check = now;
            if (!IsProcessAlive(pid)) {
                set_text("Netease closed, relay stopped");
                fatal = true;
                break;
            }
        }
    }

    render.Close();
    cap.Close();

    publish([&](RelayStatus& st) {
        st.running = false;
        st.capturing = false;
        st.rendering = false;
    });

    cleanup();
}

} // namespace audio
