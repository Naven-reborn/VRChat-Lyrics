#include "wasapi_render.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <combaseapi.h>
#include <cstring>

// Hand-defined to avoid pulling ksmedia.h (which conflicts with cguid.h's
// __uuidof when INITGUID gets activated by a chain of headers).
static const GUID kKsDataFormatSubtypeIeeeFloat =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

#ifndef SPEAKER_FRONT_LEFT
#define SPEAKER_FRONT_LEFT   0x1
#define SPEAKER_FRONT_RIGHT  0x2
#define SPEAKER_FRONT_CENTER 0x4
#endif

namespace audio {

namespace {

void FillFloat32(WAVEFORMATEXTENSIBLE& wf, uint32_t sr, uint16_t channels) {
    std::memset(&wf, 0, sizeof(wf));
    wf.Format.wFormatTag       = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels        = channels;
    wf.Format.nSamplesPerSec   = sr;
    wf.Format.wBitsPerSample   = 32;
    wf.Format.nBlockAlign      = (WORD)(channels * 4);
    wf.Format.nAvgBytesPerSec  = sr * wf.Format.nBlockAlign;
    wf.Format.cbSize           = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask = (channels == 2)
        ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
        : SPEAKER_FRONT_CENTER;
    wf.SubFormat = kKsDataFormatSubtypeIeeeFloat;
}

void SetErr(std::string& dst, const char* prefix, HRESULT hr) {
    char buf[128];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s (hr=0x%08lX)", prefix, (long)hr);
    dst = buf;
}

} // namespace

bool WasapiRender::Open(const std::wstring& device_id,
                        const WAVEFORMATEXTENSIBLE& src_fmt,
                        std::string& err) {
    Close();

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr) || !enumerator) { SetErr(err, "MMDeviceEnumerator", hr); return false; }

    IMMDevice* dev = nullptr;
    if (device_id.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
    } else {
        hr = enumerator->GetDevice(device_id.c_str(), &dev);
    }
    enumerator->Release();
    if (FAILED(hr) || !dev) { SetErr(err, "GetDevice", hr); return false; }

    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client_);
    dev->Release();
    if (FAILED(hr) || !client_) { SetErr(err, "Activate IAudioClient", hr); return false; }

    const DWORD flags =
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    auto try_init = [&](const WAVEFORMATEXTENSIBLE& fmt) -> HRESULT {
        // 10 ms requested period; engine may extend it. Pass 0 for period
        // when using EVENTCALLBACK + AUTOCONVERTPCM.
        return client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                   0, 0, (WAVEFORMATEX*)&fmt, nullptr);
    };

    WAVEFORMATEXTENSIBLE attempt = src_fmt;
    hr = try_init(attempt);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
        // Retry at engine mix-format sample rate, still float32 stereo.
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(client_->GetMixFormat(&mix)) && mix) {
            FillFloat32(attempt, mix->nSamplesPerSec, src_fmt.Format.nChannels);
            CoTaskMemFree(mix);
            hr = try_init(attempt);
        }
    }
    if (FAILED(hr)) { SetErr(err, "IAudioClient::Initialize", hr); Close(); return false; }

    sr_       = attempt.Format.nSamplesPerSec;
    channels_ = attempt.Format.nChannels;

    hr = client_->GetBufferSize(&buffer_frames_);
    if (FAILED(hr)) { SetErr(err, "GetBufferSize", hr); Close(); return false; }

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) { SetErr(err, "CreateEvent", HRESULT_FROM_WIN32(GetLastError())); Close(); return false; }

    hr = client_->SetEventHandle(event_);
    if (FAILED(hr)) { SetErr(err, "SetEventHandle", hr); Close(); return false; }

    hr = client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_);
    if (FAILED(hr) || !render_) { SetErr(err, "GetService RenderClient", hr); Close(); return false; }

    hr = client_->Start();
    if (FAILED(hr)) { SetErr(err, "Start", hr); Close(); return false; }
    started_ = true;
    return true;
}

void WasapiRender::Close() {
    if (started_ && client_) { client_->Stop(); started_ = false; }
    if (render_)  { render_->Release(); render_ = nullptr; }
    if (client_)  { client_->Release(); client_ = nullptr; }
    if (event_)   { CloseHandle(event_); event_ = nullptr; }
    buffer_frames_ = 0;
    sr_ = channels_ = 0;
}

uint32_t WasapiRender::Push(const float* src, uint32_t frames, bool& ok, std::string& err) {
    ok = true;
    if (!client_ || !render_ || frames == 0) return 0;

    UINT32 padding = 0;
    HRESULT hr = client_->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
        ok = false;
        SetErr(err, (hr == AUDCLNT_E_DEVICE_INVALIDATED) ? "device invalidated" : "GetCurrentPadding", hr);
        return 0;
    }
    UINT32 free_frames = buffer_frames_ - padding;
    if (free_frames == 0) return 0;
    UINT32 to_write = frames < free_frames ? frames : free_frames;

    BYTE* buf = nullptr;
    hr = render_->GetBuffer(to_write, &buf);
    if (FAILED(hr) || !buf) {
        ok = false;
        SetErr(err, (hr == AUDCLNT_E_DEVICE_INVALIDATED) ? "device invalidated" : "render GetBuffer", hr);
        return 0;
    }
    std::memcpy(buf, src, (size_t)to_write * channels_ * sizeof(float));
    hr = render_->ReleaseBuffer(to_write, 0);
    if (FAILED(hr)) {
        ok = false;
        SetErr(err, "render ReleaseBuffer", hr);
        return 0;
    }
    return to_write;
}

void WasapiRender::DrainSilence(uint32_t frames) {
    if (!client_ || !render_ || frames == 0) return;
    UINT32 padding = 0;
    if (FAILED(client_->GetCurrentPadding(&padding))) return;
    UINT32 free_frames = buffer_frames_ - padding;
    UINT32 to_write = frames < free_frames ? frames : free_frames;
    if (to_write == 0) return;
    BYTE* buf = nullptr;
    if (FAILED(render_->GetBuffer(to_write, &buf)) || !buf) return;
    std::memset(buf, 0, (size_t)to_write * channels_ * sizeof(float));
    render_->ReleaseBuffer(to_write, 0);
}

} // namespace audio
