#pragma once
#include <Windows.h>
#include <d3d11.h>

namespace host {

class ImGuiHost {
public:
    bool Init(HWND hwnd, ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();

    void NewFrame();
    void Render();

    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM, bool& handled);
};

}
