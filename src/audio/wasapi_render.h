#pragma once
#include <Windows.h>
#include <mmreg.h>
#include <string>
#include <cstdint>

struct IAudioClient;
struct IAudioRenderClient;

namespace audio {

// Opens a shared-mode WASAPI render stream on the specified endpoint.
// Always tries float32 stereo @ src_fmt.SamplesPerSec first, then falls
// back to the engine mix-format sample rate if init fails. AUTOCONVERTPCM
// + SRC_DEFAULT_QUALITY lets the engine reconcile bit depth / SR.
class WasapiRender {
public:
    bool Open(const std::wstring& device_id,
              const WAVEFORMATEXTENSIBLE& src_fmt,
              std::string& err);

    void Close();

    // Push up to `frames` from src (interleaved float). Returns frames
    // actually written. May write less if the device buffer is full.
    // On AUDCLNT_E_DEVICE_INVALIDATED, sets ok=false and err string.
    uint32_t Push(const float* src, uint32_t frames, bool& ok, std::string& err);

    // Write zero-filled frames to prime the buffer.
    void DrainSilence(uint32_t frames);

    HANDLE Event() const { return event_; }

    uint32_t SampleRate() const { return sr_; }
    uint32_t Channels()   const { return channels_; }

private:
    IAudioClient*       client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    HANDLE              event_  = nullptr;
    uint32_t            buffer_frames_ = 0;
    uint32_t            sr_ = 0;
    uint32_t            channels_ = 0;
    bool                started_ = false;
};

} // namespace audio
