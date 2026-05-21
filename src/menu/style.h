#pragma once
#include "imgui.h"

namespace menu {

enum class Theme { Dark = 0, Light = 1 };

namespace col {
    inline constexpr ImVec4 from_rgba(float r, float g, float b, float a = 255.f) {
        return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
    }

    // Runtime palette — updated by ApplyTheme() at startup and whenever the
    // user toggles between Dark and Light.
    extern ImVec4 bg_root;
    extern ImVec4 bg_sidebar;
    extern ImVec4 bg_content;
    extern ImVec4 bg_card;
    extern ImVec4 bg_input;
    extern ImVec4 bg_titlebar;
    extern ImVec4 bg_hover;    // subtle hover-state fill on icon buttons
    extern ImVec4 stroke;

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

extern float ui_scale;
inline float S(float v) { return v * ui_scale; }

void LoadFontsAndStyle();
void ApplyTheme(Theme t);              // snap immediately (used at startup)
void BeginThemeTransition(Theme from, Theme to);
void TickThemeTransition(float dt);    // call once per frame

}
