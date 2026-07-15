#include "style.h"
#include "MuseoSans700.h"
#include "MuseoSans900.h"
#include <Windows.h>
#include <string>

namespace menu {

ImFont* font_body    = nullptr;
ImFont* font_caption = nullptr;
ImFont* font_medium  = nullptr;
ImFont* font_title   = nullptr;
ImFont* font_logo    = nullptr;
float   ui_scale     = 1.0f;

namespace col {
    ImVec4 bg_root, bg_sidebar, bg_content, bg_card, bg_input, bg_titlebar, bg_hover;
    ImVec4 stroke;
    ImVec4 text, text_dim, text_caption;
    ImVec4 accent, accent_dim;
    ImVec4 dot_off, dot_on;
}

static void MergeCjkInto(ImGuiIO& io, float px) {
    static std::string yahei_path;
    if (yahei_path.empty()) {
        wchar_t wbuf[MAX_PATH] = {};
        UINT n = GetWindowsDirectoryW(wbuf, MAX_PATH);
        if (n == 0) return;
        std::wstring wpath = std::wstring(wbuf) + L"\\Fonts\\msyh.ttc";
        char buf[MAX_PATH * 2] = {};
        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
        yahei_path = buf;
    }
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.GlyphRanges = io.Fonts->GetGlyphRangesChineseFull();
    cfg.FontNo = 0;
    io.Fonts->AddFontFromFileTTF(yahei_path.c_str(), px, &cfg);
}

// 合并 Segoe UI Emoji,覆盖 BMP 符号块 + 补充平面所有常用 emoji 块。
// 需要 IMGUI_USE_WCHAR32 才能用 > U+FFFF 的码点(在 vcxproj 里打开了)。
// 注意:SegoeUIEmoji 主体是彩色 emoji(COLR/CBDT),stb_truetype 不支持彩色渲染,
// 只能拿到 'glyf' 表里的灰度 outline 层 —— 绝大多数 emoji 的基础轮廓还在,
// 渲染出来是单色剪影,够用。
static void MergeEmojiInto(ImGuiIO& io, float px) {
    static std::string emoji_path;
    static bool tried_path = false;
    if (!tried_path) {
        tried_path = true;
        wchar_t wbuf[MAX_PATH] = {};
        UINT n = GetWindowsDirectoryW(wbuf, MAX_PATH);
        if (n) {
            std::wstring wpath = std::wstring(wbuf) + L"\\Fonts\\seguiemj.ttf";
            char buf[MAX_PATH * 2] = {};
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
            emoji_path = buf;
        }
    }
    if (emoji_path.empty()) return;

    // 静态常量,数组一次性建好,后续合并都引用同一份 —— ImGui 要求 ranges 指针
    // 在 Build 之前保持有效。
    static const ImWchar emoji_ranges[] = {
        0x0023,  0x0023,    // # — 在 keycap emoji 序列里用到
        0x002A,  0x002A,    // *
        0x0030,  0x0039,    // 0-9 (keycap digits)
        0x00A9,  0x00A9,    // ©
        0x00AE,  0x00AE,    // ®
        0x203C,  0x203C,    // ‼
        0x2049,  0x2049,    // ⁉
        0x2122,  0x2122,    // ™
        0x2139,  0x2139,    // ℹ
        0x2194,  0x2199,    // 箭头
        0x21A9,  0x21AA,    // ↩ ↪
        0x231A,  0x231B,    // ⌚ ⌛
        0x2328,  0x2328,    // ⌨ keyboard
        0x23CF,  0x23CF,    // ⏏
        0x23E9,  0x23F3,    // 媒体控制键
        0x23F8,  0x23FA,    // ⏸ ⏹ ⏺
        0x24C2,  0x24C2,    // Ⓜ
        0x25AA,  0x25AB,    // ▪ ▫
        0x25B6,  0x25B6,    // ▶
        0x25C0,  0x25C0,    // ◀
        0x25FB,  0x25FE,    // ◻ ◼ ◽ ◾
        0x2600,  0x27BF,    // Misc Symbols + Dingbats (☀☂☎♀♣♪♫♬★☆⌨⚙⚠❤❄✂✈✉ ...)
        0x2934,  0x2935,    // ⤴ ⤵
        0x2B05,  0x2B07,    // ⬅ ⬆ ⬇
        0x2B1B,  0x2B1C,    // ⬛ ⬜
        0x2B50,  0x2B50,    // ⭐
        0x2B55,  0x2B55,    // ⭕
        0x3030,  0x3030,    // 〰
        0x303D,  0x303D,    // 〽
        0x3297,  0x3297,    // ㊗
        0x3299,  0x3299,    // ㊙
        0x1F004, 0x1F004,   // 🀄
        0x1F0CF, 0x1F0CF,   // 🃏
        0x1F170, 0x1F251,   // 🅰 等
        0x1F300, 0x1F5FF,   // Misc Symbols & Pictographs (🌐 🌟 🍀 🎮 🎵 🎬 ...)
        0x1F600, 0x1F64F,   // Emoticons (😀 😂 🙏 ...)
        0x1F680, 0x1F6FF,   // Transport & Map (🚀 🚗 🚶 ...)
        0x1F700, 0x1F77F,   // Alchemical
        0x1F780, 0x1F7FF,   // Geometric Shapes Extended
        0x1F800, 0x1F8FF,   // Supplemental Arrows-C
        0x1F900, 0x1F9FF,   // Supplemental Symbols & Pictographs (🤖 🧭 🧠 ...)
        0x1FA00, 0x1FAFF,   // Symbols and Pictographs Extended-A
        0,
    };

    ImFontConfig cfg;
    cfg.MergeMode  = true;
    cfg.PixelSnapH = true;
    // emoji 字形已经很大,oversample 没用还浪费 atlas 空间。
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.GlyphRanges = emoji_ranges;
    cfg.FontNo      = 0;
    io.Fonts->AddFontFromFileTTF(emoji_path.c_str(), px, &cfg);
}

static void ApplyImGuiStyleColors() {
    ImGuiStyle& s = ImGui::GetStyle();
    auto* c = s.Colors;
    c[ImGuiCol_Text]              = col::text;
    c[ImGuiCol_TextDisabled]      = col::text_dim;
    c[ImGuiCol_WindowBg]          = col::bg_content;
    c[ImGuiCol_ChildBg]           = col::bg_content;
    // 亮色弹层用 input 底,避免跟白卡糊在一起;暗色仍用 card。
    const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
    c[ImGuiCol_PopupBg]           = lightish ? col::bg_input : col::bg_card;
    c[ImGuiCol_Border]            = col::stroke;
    c[ImGuiCol_FrameBg]           = col::bg_input;
    c[ImGuiCol_FrameBgHovered]    = col::bg_hover;
    c[ImGuiCol_FrameBgActive]     = col::bg_hover;
    c[ImGuiCol_ScrollbarBg]       = col::bg_content;
    c[ImGuiCol_ScrollbarGrab]     = lightish ? col::stroke : col::stroke;
    c[ImGuiCol_ScrollbarGrabHovered] = col::text_dim;
    c[ImGuiCol_ScrollbarGrabActive]  = col::accent_dim;
    c[ImGuiCol_CheckMark]         = col::accent;
    c[ImGuiCol_SliderGrab]        = col::text;
    c[ImGuiCol_SliderGrabActive]  = col::text;
    c[ImGuiCol_Button]            = col::bg_input;
    c[ImGuiCol_ButtonHovered]     = col::bg_hover;
    c[ImGuiCol_ButtonActive]      = col::stroke;
    c[ImGuiCol_Header]            = col::bg_input;
    c[ImGuiCol_HeaderHovered]     = col::bg_hover;
    c[ImGuiCol_HeaderActive]      = col::bg_hover;
    c[ImGuiCol_Separator]         = col::stroke;
    c[ImGuiCol_ResizeGrip]        = ImVec4(0, 0, 0, 0);
}

void ApplyTheme(Theme t) {
    using col::from_rgba;
    if (t == Theme::Light) {
        // 亮色主题重做(v3.3):
        // 旧版几乎全是 240–255 的冷灰白,卡片/输入/背景糊成一片,看起来像"白茫茫"。
        // 新方向:干净的暖灰内容区 + 纯白 elevated 卡片 + 明确边框层次 + 克制蓝 accent。
        // 仍保留圆角与蓝色 accent(用户硬规则),不走 vermilion / 硬角。
        //
        // 层级(从底到顶):
        //   content/root  #F0F2F5  冷灰底
        //   sidebar/title #E8ECF1  再暗一档,跟内容区分开
        //   card          #FFFFFF  浮起
        //   input         #F4F6F9  卡片内再凹一档
        //   hover         #E4EAF2  可交互反馈
        //   stroke        #D5DCE6  可见但不抢
        col::bg_root      = from_rgba(240.f, 242.f, 245.f, 255.f);
        col::bg_sidebar   = from_rgba(232.f, 236.f, 241.f, 255.f);
        col::bg_content   = from_rgba(240.f, 242.f, 245.f, 255.f);
        col::bg_card      = from_rgba(255.f, 255.f, 255.f, 255.f);
        col::bg_input     = from_rgba(244.f, 246.f, 249.f, 255.f);
        col::bg_titlebar  = from_rgba(232.f, 236.f, 241.f, 255.f);
        col::bg_hover     = from_rgba(228.f, 234.f, 242.f, 255.f);
        col::stroke       = from_rgba(213.f, 220.f, 230.f, 255.f);

        // 文字对比度拉高:主字近黑,次级灰,caption 再淡 —— 亮底上 dim 不能太浅。
        col::text         = from_rgba( 22.f,  28.f,  38.f, 255.f);
        col::text_dim     = from_rgba( 88.f, 100.f, 118.f, 255.f);
        col::text_caption = from_rgba(120.f, 132.f, 150.f, 255.f);

        // 亮色 accent 用稍深的青蓝,保证在白底上可读(旧 #2086B8 偏灰,不够利落)。
        col::accent       = from_rgba( 28.f, 132.f, 196.f, 255.f); // #1C84C4
        col::accent_dim   = from_rgba(120.f, 170.f, 205.f, 255.f);

        col::dot_off      = from_rgba(160.f, 170.f, 184.f, 255.f);
        col::dot_on       = from_rgba(255.f, 255.f, 255.f, 255.f);
    } else {
        // 暖中性 off-black,不是纯 #000;input 比 root 略亮一档,card 再亮一档。
        col::bg_root      = from_rgba( 14.f,  14.f,  14.f, 255.f);
        col::bg_sidebar   = from_rgba( 10.f,  10.f,  10.f, 255.f);
        col::bg_content   = from_rgba( 14.f,  14.f,  14.f, 255.f);
        col::bg_card      = from_rgba( 22.f,  22.f,  22.f, 255.f);
        col::bg_input     = from_rgba( 18.f,  18.f,  18.f, 255.f);
        col::bg_titlebar  = from_rgba( 10.f,  10.f,  10.f, 255.f);
        col::bg_hover     = from_rgba( 32.f,  32.f,  32.f, 255.f);
        col::stroke       = from_rgba( 45.f,  45.f,  45.f, 255.f);

        col::text         = from_rgba(242.f, 242.f, 240.f, 255.f);
        col::text_dim     = from_rgba(140.f, 140.f, 138.f, 255.f);
        col::text_caption = from_rgba( 95.f,  95.f,  93.f, 255.f);

        col::accent       = from_rgba(121.f, 200.f, 235.f, 255.f);
        col::accent_dim   = from_rgba( 70.f, 120.f, 150.f, 255.f);

        col::dot_off      = from_rgba(110.f, 124.f, 140.f, 255.f);
        col::dot_on       = from_rgba(255.f, 255.f, 255.f, 255.f);
    }
    ApplyImGuiStyleColors();
}

// 主题切换 —— 300ms ease-out cubic 把整个调色板 lerp 过去。
namespace {
    constexpr int kPalN = 15;
    struct Snap { ImVec4 c[kPalN]; };

    Snap CaptureFor(Theme t) {
        ApplyTheme(t);   // sets col::* to that theme
        Snap s;
        s.c[0]  = col::bg_root;    s.c[1]  = col::bg_sidebar;
        s.c[2]  = col::bg_content; s.c[3]  = col::bg_card;
        s.c[4]  = col::bg_input;   s.c[5]  = col::bg_titlebar;
        s.c[6]  = col::bg_hover;   s.c[7]  = col::stroke;
        s.c[8]  = col::text;       s.c[9]  = col::text_dim;
        s.c[10] = col::text_caption;
        s.c[11] = col::accent;     s.c[12] = col::accent_dim;
        s.c[13] = col::dot_off;    s.c[14] = col::dot_on;
        return s;
    }

    void RestoreFromSnap(const Snap& s) {
        col::bg_root      = s.c[0];  col::bg_sidebar = s.c[1];
        col::bg_content   = s.c[2];  col::bg_card    = s.c[3];
        col::bg_input     = s.c[4];  col::bg_titlebar= s.c[5];
        col::bg_hover     = s.c[6];  col::stroke     = s.c[7];
        col::text         = s.c[8];  col::text_dim   = s.c[9];
        col::text_caption = s.c[10];
        col::accent       = s.c[11]; col::accent_dim = s.c[12];
        col::dot_off      = s.c[13]; col::dot_on     = s.c[14];
        ApplyImGuiStyleColors();
    }

    ImVec4 LerpVec(const ImVec4& a, const ImVec4& b, float t) {
        return { a.x + (b.x - a.x) * t,
                 a.y + (b.y - a.y) * t,
                 a.z + (b.z - a.z) * t,
                 a.w + (b.w - a.w) * t };
    }

    Snap  g_from, g_to;
    float g_trans_t = 1.f;
}

void BeginThemeTransition(Theme from, Theme to) {
    g_from = CaptureFor(from);
    g_to   = CaptureFor(to);
    g_trans_t = 0.f;
    RestoreFromSnap(g_from);
}

void TickThemeTransition(float dt) {
    if (g_trans_t >= 1.f) return;
    g_trans_t += dt / 0.30f;
    if (g_trans_t > 1.f) g_trans_t = 1.f;
    float u = 1.f - g_trans_t;
    float t = 1.f - u * u * u;  // ease-out cubic
    Snap cur;
    for (int i = 0; i < kPalN; ++i) cur.c[i] = LerpVec(g_from.c[i], g_to.c[i], t);
    RestoreFromSnap(cur);
}

void LoadFontsAndStyle() {
    ImGuiIO& io = ImGui::GetIO();

    static const ImWchar ranges[] = {
        0x0020, 0x00FF,
        0x0400, 0x052F,
        0x2DE0, 0x2DFF,
        0xA640, 0xA69F,
        0,
    };

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH  = false;
    cfg.RasterizerMultiply = 1.05f;
    cfg.GlyphRanges = ranges;
    cfg.FontDataOwnedByAtlas = false;

    font_caption = io.Fonts->AddFontFromMemoryTTF(chMuseoSans700, sizeof(chMuseoSans700), 11.f * ui_scale, &cfg);
    MergeCjkInto(io, 11.f * ui_scale);
    MergeEmojiInto(io, 11.f * ui_scale);
    font_body    = io.Fonts->AddFontFromMemoryTTF(chMuseoSans700, sizeof(chMuseoSans700), 14.f * ui_scale, &cfg);
    MergeCjkInto(io, 14.f * ui_scale);
    MergeEmojiInto(io, 14.f * ui_scale);
    font_medium  = io.Fonts->AddFontFromMemoryTTF(chMuseoSans700, sizeof(chMuseoSans700), 16.f * ui_scale, &cfg);
    MergeCjkInto(io, 16.f * ui_scale);
    MergeEmojiInto(io, 16.f * ui_scale);
    font_title   = io.Fonts->AddFontFromMemoryTTF(chMuseoSans700, sizeof(chMuseoSans700), 20.f * ui_scale, &cfg);
    MergeCjkInto(io, 20.f * ui_scale);
    MergeEmojiInto(io, 20.f * ui_scale);
    font_logo    = io.Fonts->AddFontFromMemoryTTF(chMuseoSans900, sizeof(chMuseoSans900), 18.f * ui_scale, &cfg);

    io.FontDefault = font_body;

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.f;
    s.ChildRounding     = 4.f;
    s.FrameRounding     = 4.f;
    s.GrabRounding      = 4.f;
    s.PopupRounding     = 4.f;
    s.ScrollbarRounding = 4.f;
    s.WindowBorderSize  = 0.f;
    s.ChildBorderSize   = 0.f;
    s.FrameBorderSize   = 0.f;
    s.FramePadding      = ImVec2(6.f, 4.f);
    s.ItemSpacing       = ImVec2(8.f, 6.f);
    s.ScrollbarSize     = 8.f;
    s.AntiAliasedLines  = true;
    s.AntiAliasedFill   = true;
    s.ScaleAllSizes(ui_scale);

    ApplyTheme(Theme::Dark);
}

}
