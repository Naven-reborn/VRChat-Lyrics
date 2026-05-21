#include "imgui_host.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace host {

bool ImGuiHost::Init(HWND hwnd, ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.WindowBorderSize = 0.0f;

    FILE* f = nullptr; fopen_s(&f, "vrc-lyrics.log", "a");
    if (!ImGui_ImplWin32_Init(hwnd)) {
        if (f) { fputs("ImGui_ImplWin32_Init false\n", f); fclose(f); }
        return false;
    }
    if (f) { fputs("Win32 init ok\n", f); }
    if (!ImGui_ImplDX11_Init(dev, ctx)) {
        if (f) { fputs("ImGui_ImplDX11_Init false\n", f); fclose(f); }
        return false;
    }
    if (f) { fputs("DX11 init ok\n", f); fclose(f); }
    return true;
}

void ImGuiHost::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    // ImGui docking 分支的一个坑:ImGui_ImplWin32_Shutdown 没清 main viewport
    // 的 PlatformHandle,而 ImGui::Shutdown() 后面有 assert 要求它必须是 null。
    // 不手动清就 abort 退出码 3。
    if (ImGuiViewport* vp = ImGui::GetMainViewport()) {
        vp->PlatformHandle = nullptr;
        vp->PlatformHandleRaw = nullptr;
    }
    ImGui::DestroyContext();
}

void ImGuiHost::NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiHost::Render() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

LRESULT ImGuiHost::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
        handled = true;
        return 0;
    }
    handled = false;
    return 0;
}

}
