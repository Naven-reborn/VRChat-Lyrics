#include "process_loopback.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <combaseapi.h>
#include <cstring>

namespace audio {

namespace {

void SetErr(std::string& dst, const char* prefix, HRESULT hr) {
    char buf[128];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s (hr=0x%08lX)", prefix, (long)hr);
    dst = buf;
}

// Minimal hand-rolled COM object for IActivateAudioInterfaceCompletionHandler.
// Refcounted, agile-by-construction (we only ever signal an event from the
// callback — no STA marshalling required).
class ActivationHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE   event{};      // manual-reset, set when activation completes
    HRESULT  hr_result{ E_PENDING };
    IUnknown* iface{};     // ref counted, caller takes ownership

    ActivationHandler() {
        event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~ActivationHandler() {
        if (iface) { iface->Release(); iface = nullptr; }
        if (event) { CloseHandle(event); event = nullptr; }
    }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&ref_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IActivateAudioInterfaceCompletionHandler) ||
            riid == __uuidof(IAgileObject)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT act_hr = E_FAIL;
        IUnknown* obj = nullptr;
        HRESULT op_hr = op->GetActivateResult(&act_hr, &obj);
        if (SUCCEEDED(op_hr) && SUCCEEDED(act_hr) && obj) {
            iface = obj;     // transfer
            hr_result = S_OK;
        } else {
            if (obj) obj->Release();
            hr_result = SUCCEEDED(op_hr) ? act_hr : op_hr;
        }
        SetEvent(event);
        return S_OK;
    }

private:
    LONG ref_ = 1;
};

} // namespace

bool ProcessLoopbackCapture::Open(uint32_t root_pid,
                                  const WAVEFORMATEXTENSIBLE& fmt,
                                  std::string& err) {
    Close();
    channels_ = fmt.Format.nChannels;

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId    = (DWORD)root_pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    PropVariantInit(&pv);
    pv.vt           = VT_BLOB;
    pv.blob.cbSize  = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivationHandler* h = new ActivationHandler();
    if (!h->event) { delete h; err = "CreateEvent failed"; return false; }

    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &pv,
        h,
        &op);
    // Intentionally do NOT PropVariantClear(&pv) — blob points at stack data.
    if (FAILED(hr) || !op) {
        h->Release();
        SetErr(err, "ActivateAudioInterfaceAsync", hr);
        return false;
    }
    op->Release(); // we hold completion via the handler

    DWORD wait = WaitForSingleObject(h->event, 3000);
    if (wait != WAIT_OBJECT_0) {
        h->Release();
        err = "Activation timeout";
        return false;
    }
    if (FAILED(h->hr_result) || !h->iface) {
        HRESULT act_hr = h->hr_result;
        h->Release();
        SetErr(err, "Activation failed", act_hr);
        return false;
    }

    // Transfer the IUnknown — QI for IAudioClient.
    hr = h->iface->QueryInterface(__uuidof(IAudioClient), (void**)&client_);
    h->Release(); // releases iface as part of dtor
    if (FAILED(hr) || !client_) { SetErr(err, "QI IAudioClient", hr); return false; }

    const DWORD flags =
        AUDCLNT_STREAMFLAGS_LOOPBACK |
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    // 200000 hns = 20 ms requested buffer. Period must be 0 with EVENTCALLBACK
    // in shared mode for process loopback.
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                             2000000, 0, (WAVEFORMATEX*)&fmt, nullptr);
    if (FAILED(hr)) { SetErr(err, "loopback Initialize", hr); Close(); return false; }

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) { err = "CreateEvent failed"; Close(); return false; }
    hr = client_->SetEventHandle(event_);
    if (FAILED(hr)) { SetErr(err, "SetEventHandle", hr); Close(); return false; }

    hr = client_->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_);
    if (FAILED(hr) || !capture_) { SetErr(err, "GetService Capture", hr); Close(); return false; }

    hr = client_->Start();
    if (FAILED(hr)) { SetErr(err, "Start", hr); Close(); return false; }
    started_ = true;
    return true;
}

void ProcessLoopbackCapture::Close() {
    if (started_ && client_) { client_->Stop(); started_ = false; }
    if (capture_) { capture_->Release(); capture_ = nullptr; }
    if (client_)  { client_->Release(); client_ = nullptr; }
    if (event_)   { CloseHandle(event_); event_ = nullptr; }
    channels_ = 0;
}

uint32_t ProcessLoopbackCapture::Pull(float* dst, uint32_t max_frames,
                                      bool& silent, bool& ok, std::string& err) {
    silent = true;
    ok = true;
    if (!capture_ || !dst || max_frames == 0) return 0;

    uint32_t written = 0;
    UINT32 next = 0;
    HRESULT hr = capture_->GetNextPacketSize(&next);
    if (FAILED(hr)) {
        ok = false;
        SetErr(err, (hr == AUDCLNT_E_DEVICE_INVALIDATED)    ? "device invalidated" :
                    (hr == AUDCLNT_E_RESOURCES_INVALIDATED) ? "resources invalidated" :
                                                              "GetNextPacketSize",
               hr);
        return 0;
    }
    while (next > 0 && written < max_frames) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
        if (FAILED(hr)) {
            ok = false;
            SetErr(err, (hr == AUDCLNT_E_DEVICE_INVALIDATED)    ? "device invalidated" :
                        (hr == AUDCLNT_E_RESOURCES_INVALIDATED) ? "resources invalidated" :
                                                                  "capture GetBuffer",
                   hr);
            return written;
        }
        // WASAPI 不允许部分消费一个包。要么整包 ReleaseBuffer(frames),要么 (0)
        // 把这一包留给下一次 GetBuffer 处理。早先的代码 take = max_frames - written
        // 之后还 ReleaseBuffer(frames),WASAPI 把整包标记为已读 —— 等于把 frames-take
        // 那段音频静悄悄丢了,造成启动时的爆音/中断。
        if (written + frames > max_frames) {
            capture_->ReleaseBuffer(0);
            break;
        }
        const size_t bytes = (size_t)frames * channels_ * sizeof(float);
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::memset(dst + (size_t)written * channels_, 0, bytes);
        } else {
            std::memcpy(dst + (size_t)written * channels_, data, bytes);
            silent = false;
        }
        written += frames;
        capture_->ReleaseBuffer(frames);

        if (FAILED(capture_->GetNextPacketSize(&next))) break;
    }
    return written;
}

} // namespace audio
