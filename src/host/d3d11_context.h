#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

namespace host {

// 始终走 CreateSwapChainForComposition + DirectComposition。
// 这样 Dark/Light/Blur 切换时不销毁 HWND swapchain,DWM 不会把
// "上一主题的不透明画面" 缓存成 redirection 垫在 Acrylic 下面。
// 不透明主题: clear alpha=1; 毛玻璃: clear 半透明预乘 + 系统 Acrylic。
class D3D11Context {
public:
    bool Create(HWND hwnd);
    void Destroy();

    void Resize(int w, int h);

    // 兼容旧调用:现在 composition 路径始终开启,此函数只记录期望状态。
    // 返回 true 表示 composition 可用。
    bool SetPremultipliedAlpha(bool enable);
    bool PremultipliedAlpha() const { return m_composition; }
    bool CompositionActive() const { return m_composition && m_swap; }

    void BeginFrame(const float clear_rgba[4]);
    void Present(bool vsync);

    // 把两个 flip buffer 都清成透明,主题切换后立刻调用,避免旧帧闪一下。
    void ClearAllBuffers(const float clear_rgba[4]);

    ID3D11Device*        Device() const  { return m_device; }
    ID3D11DeviceContext* Context() const { return m_ctx; }

private:
    bool CreateSwapChain();
    void CreateRenderTarget();
    void ReleaseRenderTarget();
    void ReleaseComposition();
    bool SetupComposition();
    void ClientSize(int& w, int& h) const;

    ID3D11Device*           m_device  = nullptr;
    ID3D11DeviceContext*    m_ctx     = nullptr;
    IDXGISwapChain1*        m_swap    = nullptr;
    ID3D11RenderTargetView* m_rtv     = nullptr;
    HWND                    m_hwnd    = nullptr;
    bool                    m_composition = false;
    bool                    m_want_premul = false;

    IDCompositionDevice*    m_dcomp        = nullptr;
    IDCompositionTarget*    m_dcomp_target = nullptr;
    IDCompositionVisual*    m_dcomp_visual = nullptr;
};

}
