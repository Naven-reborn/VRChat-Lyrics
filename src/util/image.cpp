#include "image.h"

#include <Windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <cmath>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace util {

static IWICImagingFactory* g_wic = nullptr;
static bool g_com_inited = false;

static bool EnsureWIC() {
    if (g_wic) return true;
    if (!g_com_inited) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        g_com_inited = true;
    }
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    return SUCCEEDED(hr);
}

static void ApplyCircleMask(uint8_t* rgba, int w, int h) {
    float cx = w * 0.5f, cy = h * 0.5f;
    float r  = (w < h ? cx : cy);
    float aa = 1.5f; // edge softness in pixels
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d  = std::sqrt(dx * dx + dy * dy);
            uint8_t& a = rgba[(y * w + x) * 4 + 3];
            if (d > r) {
                a = 0;
            } else if (d > r - aa) {
                float t = (r - d) / aa;
                if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
                a = (uint8_t)(a * t);
            }
        }
    }
}

ID3D11ShaderResourceView* CreateCircularTexture(ID3D11Device* device,
                                                 const uint8_t* data, size_t size,
                                                 int target_px) {
    if (!device || !data || size < 4) return nullptr;
    if (!EnsureWIC()) return nullptr;

    IWICStream* stream = nullptr;
    if (FAILED(g_wic->CreateStream(&stream))) return nullptr;
    stream->InitializeFromMemory((WICInProcPointer)const_cast<uint8_t*>(data), (DWORD)size);

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_wic->CreateDecoderFromStream(stream, nullptr,
                    WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) return nullptr;

    UINT sw = 0, sh = 0;
    frame->GetSize(&sw, &sh);
    if (sw == 0 || sh == 0) { frame->Release(); return nullptr; }

    // 转成 32bpp RGBA
    IWICFormatConverter* conv = nullptr;
    g_wic->CreateFormatConverter(&conv);
    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                          nullptr, 0.f, WICBitmapPaletteTypeCustom);
    frame->Release();
    if (FAILED(hr)) { conv->Release(); return nullptr; }

    // 缩放到 target_px 正方形
    UINT tw = (UINT)target_px, th = (UINT)target_px;
    IWICBitmapScaler* scaler = nullptr;
    g_wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(conv, tw, th, WICBitmapInterpolationModeFant);
    conv->Release();

    std::vector<uint8_t> rgba((size_t)tw * th * 4);
    hr = scaler->CopyPixels(nullptr, tw * 4, (UINT)rgba.size(), rgba.data());
    scaler->Release();
    if (FAILED(hr)) return nullptr;

    ApplyCircleMask(rgba.data(), (int)tw, (int)th);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = tw; desc.Height = th;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba.data();
    init.SysMemPitch = tw * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &tex))) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = desc.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    return srv;
}

}
