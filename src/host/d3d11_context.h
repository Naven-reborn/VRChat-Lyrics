#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>

namespace host {

class D3D11Context {
public:
    bool Create(HWND hwnd);
    void Destroy();

    void Resize(int w, int h);

    void BeginFrame(const float clear_rgba[4]);
    void Present(bool vsync);

    ID3D11Device*        Device() const  { return m_device; }
    ID3D11DeviceContext* Context() const { return m_ctx; }

private:
    void CreateRenderTarget();
    void ReleaseRenderTarget();

    ID3D11Device*           m_device  = nullptr;
    ID3D11DeviceContext*    m_ctx     = nullptr;
    IDXGISwapChain1*        m_swap    = nullptr;
    ID3D11RenderTargetView* m_rtv     = nullptr;
    HWND                    m_hwnd    = nullptr;
};

}
