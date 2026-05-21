#include "d3d11_context.h"
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace host {

bool D3D11Context::Create(HWND hwnd) {
    m_hwnd = hwnd;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        want, _countof(want), D3D11_SDK_VERSION,
        &m_device, &level, &m_ctx);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            want, _countof(want), D3D11_SDK_VERSION,
            &m_device, &level, &m_ctx);
    }
    if (FAILED(hr)) return false;

    IDXGIDevice* dxgi_dev = nullptr;
    m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev);
    IDXGIAdapter* adapter = nullptr;
    dxgi_dev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0; desc.Height = 0;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    hr = factory->CreateSwapChainForHwnd(m_device, hwnd, &desc, nullptr, nullptr, &m_swap);

    factory->Release();
    adapter->Release();
    dxgi_dev->Release();

    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

void D3D11Context::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_rtv);
        back->Release();
    }
}

void D3D11Context::ReleaseRenderTarget() {
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
}

void D3D11Context::Resize(int w, int h) {
    if (!m_swap) return;
    ReleaseRenderTarget();
    m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void D3D11Context::BeginFrame(const float clear_rgba[4]) {
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_ctx->ClearRenderTargetView(m_rtv, clear_rgba);
}

void D3D11Context::Present(bool vsync) {
    m_swap->Present(vsync ? 1 : 0, 0);
}

void D3D11Context::Destroy() {
    ReleaseRenderTarget();
    if (m_swap)   { m_swap->Release();   m_swap = nullptr; }
    if (m_ctx)    { m_ctx->Release();    m_ctx = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

}
