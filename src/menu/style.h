#pragma once
#include "imgui.h"

namespace menu {

// Dark / Light opaque; Blur uses Win11 DWM Acrylic (transparent window + translucent UI).
// Falls back to Dark palette if unsupported.
enum class Theme { Dark = 0, Light = 1, Blur = 2 };

inline bool ThemeWantsBlur(Theme t) { return t == Theme::Blur; }

namespace col {
    inline constexpr ImVec4 from_rgba(float r, float g, float b, float a = 255.f) {
        return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
    }

    extern ImVec4 bg_root;
    extern ImVec4 bg_sidebar;
    extern ImVec4 bg_content;
    extern ImVec4 bg_card;
    extern ImVec4 bg_input;
    extern ImVec4 bg_titlebar;
    extern ImVec4 bg_hover;
    extern ImVec4 stroke;
    extern ImVec4 bg_popup, popup_border, popup_hover;

    extern ImVec4 text;
    extern ImVec4 text_dim;
    extern ImVec4 text_caption;

    extern ImVec4 accent;
    extern ImVec4 accent_dim;

    extern ImVec4 dot_off;
    extern ImVec4 dot_on;
}

extern ImFont* font_body;
extern ImFont* font_caption;
extern ImFont* font_medium;
extern ImFont* font_title;
extern ImFont* font_logo;
extern ImFont* font_lyrics;

extern float ui_scale;
extern bool interface_motion_enabled;
inline float S(float v) { return v * ui_scale; }

void LoadFontsAndStyle();
void ApplyTheme(Theme t, int blur_opacity = 55); // blur_opacity 0..100, Blur only
// Snap immediately and cancel any 300ms lerp.
void SnapTheme(Theme t, int blur_opacity = 55);
void BeginThemeTransition(Theme from, Theme to, int blur_opacity = 55);
void TickThemeTransition(float dt);
bool ThemeTransitionActive();
// 0..1 during 300ms theme lerp (eased).
float ThemeTransitionT();
// Live-update Blur panel alphas while already on Blur.
void RefreshBlurOpacity(int blur_opacity);
// Clear-tint alpha (0..1 premult) for main.cpp make_clear.
float BlurClearAlpha(int blur_opacity);

}
