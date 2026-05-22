#include "devices.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <combaseapi.h>
#include <string>
#include <cwctype>

#pragma comment(lib, "propsys.lib")

namespace audio {

namespace {

std::string Utf8FromWide(const wchar_t* w, int len = -1) {
    if (!w) return {};
    int wlen = (len >= 0) ? len : (int)wcslen(w);
    if (wlen <= 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, wlen, r.data(), n, nullptr, nullptr);
    return r;
}

bool ContainsCaseI(const std::wstring& hay, const wchar_t* needle) {
    std::wstring h = hay;
    for (auto& c : h) c = (wchar_t)towlower(c);
    std::wstring n = needle;
    for (auto& c : n) c = (wchar_t)towlower(c);
    return h.find(n) != std::wstring::npos;
}

} // namespace

std::vector<RenderDevice> EnumRenderDevices() {
    std::vector<RenderDevice> out;

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr) || !enumerator) return out;

    // Default endpoint id for the multimedia role.
    std::wstring default_id;
    IMMDevice* default_dev = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &default_dev)) && default_dev) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(default_dev->GetId(&id)) && id) {
            default_id = id;
            CoTaskMemFree(id);
        }
        default_dev->Release();
    }

    IMMDeviceCollection* coll = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) || !coll) {
        enumerator->Release();
        return out;
    }

    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(coll->Item(i, &dev)) || !dev) continue;

        RenderDevice rd{};
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id)) && id) {
            rd.id = id;
            CoTaskMemFree(id);
        }
        rd.is_default = (!rd.id.empty() && rd.id == default_id);

        IPropertyStore* props = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))
                && pv.vt == VT_LPWSTR && pv.pwszVal) {
                std::wstring wname = pv.pwszVal;
                rd.friendly_utf8 = Utf8FromWide(wname.c_str(), (int)wname.size());
                rd.is_vbcable = ContainsCaseI(wname, L"CABLE Input");
            }
            PropVariantClear(&pv);
            props->Release();
        }

        if (!rd.id.empty()) out.push_back(std::move(rd));
        dev->Release();
    }

    coll->Release();
    enumerator->Release();
    return out;
}

std::optional<RenderDevice> FindVbCable() {
    auto all = EnumRenderDevices();
    for (auto& d : all) {
        if (d.is_vbcable) return d;
    }
    return std::nullopt;
}

bool DeviceExists(const std::wstring& id) {
    if (id.empty()) return false;
    auto all = EnumRenderDevices();
    for (auto& d : all) if (d.id == id) return true;
    return false;
}

} // namespace audio
