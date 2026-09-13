#include "d3d11_context.h"
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace host {

bool D3D11Context::Create(HWND hwnd) {
    m_hwnd = hwnd;
    m_want_premul = false;
    m_composition = false;

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

    // 优先 composition(毛玻璃必需);失败回落 HWND 普通 swapchain。
    if (!CreateSwapChain()) return false;
    CreateRenderTarget();
    return true;
}

void D3D11Context::ClientSize(int& w, int& h) const {
    RECT rc{ 0, 0, 0, 0 };
    if (m_hwnd) GetClientRect(m_hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
}

void D3D11Context::ReleaseComposition() {
    if (m_dcomp_visual) { m_dcomp_visual->Release(); m_dcomp_visual = nullptr; }
    if (m_dcomp_target) { m_dcomp_target->Release(); m_dcomp_target = nullptr; }
    if (m_dcomp)        { m_dcomp->Release();        m_dcomp = nullptr; }
}

bool D3D11Context::SetupComposition() {
    if (!m_device || !m_hwnd || !m_swap) return false;
    ReleaseComposition();

    IDXGIDevice* dxgi_dev = nullptr;
    if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev)) || !dxgi_dev)
        return false;

    HRESULT hr = DCompositionCreateDevice(
        dxgi_dev, __uuidof(IDCompositionDevice), (void**)&m_dcomp);
    dxgi_dev->Release();
    if (FAILED(hr) || !m_dcomp) {
        ReleaseComposition();
        return false;
    }

    hr = m_dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcomp_target);
    if (FAILED(hr) || !m_dcomp_target) {
        ReleaseComposition();
        return false;
    }

    hr = m_dcomp->CreateVisual(&m_dcomp_visual);
    if (FAILED(hr) || !m_dcomp_visual) {
        ReleaseComposition();
        return false;
    }

    hr = m_dcomp_visual->SetContent(m_swap);
    if (FAILED(hr)) {
        ReleaseComposition();
        return false;
    }

    hr = m_dcomp_target->SetRoot(m_dcomp_visual);
    if (FAILED(hr)) {
        ReleaseComposition();
        return false;
    }

    hr = m_dcomp->Commit();
    if (FAILED(hr)) {
        ReleaseComposition();
        return false;
    }
    return true;
}

bool D3D11Context::CreateSwapChain() {
    if (!m_device || !m_hwnd) return false;

    IDXGIDevice* dxgi_dev = nullptr;
    m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev);
    if (!dxgi_dev) return false;
    IDXGIAdapter* adapter = nullptr;
    dxgi_dev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    if (adapter) adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    if (!factory) {
        if (adapter) adapter->Release();
        dxgi_dev->Release();
        return false;
    }

    int cw = 1, ch = 1;
    ClientSize(cw, ch);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = (UINT)cw;
    desc.Height = (UINT)ch;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;

    // 1) 尝试 composition + 预乘透明(Blur 主题需要,Dark/Light 也走这条避免切换拆链)
    HRESULT hr = factory->CreateSwapChainForComposition(m_device, &desc, nullptr, &m_swap);
    if (SUCCEEDED(hr) && m_swap) {
        if (SetupComposition()) {
            m_composition = true;
            factory->Release();
            if (adapter) adapter->Release();
            dxgi_dev->Release();
            return true;
        }
        m_swap->Release();
        m_swap = nullptr;
        ReleaseComposition();
    }

    // 2) 回落:普通 HWND swapchain(无毛玻璃)
    m_composition = false;
    desc.Width = 0;
    desc.Height = 0;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    hr = factory->CreateSwapChainForHwnd(m_device, m_hwnd, &desc, nullptr, nullptr, &m_swap);

    factory->Release();
    if (adapter) adapter->Release();
    dxgi_dev->Release();

    return SUCCEEDED(hr) && m_swap;
}

bool D3D11Context::SetPremultipliedAlpha(bool enable) {
    m_want_premul = enable;
    // composition 路径下始终可用;HWND 回落时 Blur 会在上层被拒绝。
    return m_composition && m_swap;
}

void D3D11Context::CreateRenderTarget() {
    if (!m_swap) return;
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
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    ReleaseRenderTarget();
    if (m_composition) {
        m_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    } else {
        m_swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    }
    CreateRenderTarget();
    if (m_dcomp) m_dcomp->Commit();
}

void D3D11Context::BeginFrame(const float clear_rgba[4]) {
    if (!m_rtv || !m_ctx) return;
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_ctx->ClearRenderTargetView(m_rtv, clear_rgba);
}

void D3D11Context::ClearAllBuffers(const float clear_rgba[4]) {
    if (!m_swap || !m_ctx) return;
    // flip 链 2 缓冲:清 + Present 两轮,把前后缓冲都刷成目标色。
    for (int i = 0; i < 2; ++i) {
        ReleaseRenderTarget();
        CreateRenderTarget();
        if (m_rtv) {
            m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
            m_ctx->ClearRenderTargetView(m_rtv, clear_rgba);
        }
        m_swap->Present(0, 0);
        if (m_dcomp) m_dcomp->Commit();
    }
    ReleaseRenderTarget();
    CreateRenderTarget();
}

void D3D11Context::Present(bool vsync) {
    if (!m_swap) return;
    m_swap->Present(vsync ? 1 : 0, 0);
    if (m_dcomp) m_dcomp->Commit();
}

void D3D11Context::Destroy() {
    ReleaseRenderTarget();
    ReleaseComposition();
    if (m_swap)   { m_swap->Release();   m_swap = nullptr; }
    if (m_ctx)    { m_ctx->Release();    m_ctx = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    m_composition = false;
}

}
