#pragma once
#include <Windows.h>
#include <mmreg.h>
#include <string>
#include <cstdint>

struct IAudioClient;
struct IAudioCaptureClient;

namespace audio {

// Per-process loopback capture using ActivateAudioInterfaceAsync +
// AUDIOCLIENT_ACTIVATION_PARAMS (Win10 build 20348+). Captures audio from
// the target PID's process tree only — sibling apps (VRChat, Discord) are
// excluded.
class ProcessLoopbackCapture {
public:
    bool Open(uint32_t root_pid,
              const WAVEFORMATEXTENSIBLE& fmt,
              std::string& err);

    void Close();

    // Pull all currently available packets, append to out_pcm (interleaved
    // float). Returns frames appended. Sets ok=false on hard errors
    // (device invalidated / resources invalidated).
    uint32_t Pull(float* dst,
                  uint32_t max_frames,
                  bool& silent,
                  bool& ok,
                  std::string& err);

    HANDLE Event() const { return event_; }

private:
    IAudioClient*        client_  = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    HANDLE               event_   = nullptr;
    uint32_t             channels_ = 0;
    bool                 started_ = false;
};

} // namespace audio
