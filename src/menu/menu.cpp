#include "menu.h"
#include "style.h"
#include "i18n/i18n.h"
#include "util/foreground.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace menu {

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static float EaseOutCubic(float t) { float u = 1.f - t; return 1.f - u * u * u; }

static float Anim(ImGuiID id, bool target, float speed = 14.f) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float cur = st->GetFloat(id, target ? 1.f : 0.f);
    float dst = target ? 1.f : 0.f;
    float t   = 1.f - std::exp(-speed * ImGui::GetIO().DeltaTime);
    cur = Lerp(cur, dst, t);
    if (std::fabs(cur - dst) < 1.f / 512.f) cur = dst;
    st->SetFloat(id, cur);
    return cur;
}

static ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
static ImU32 U32(const ImVec4& c, float alpha) {
    ImVec4 d = c; d.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(d);
}
static ImU32 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return U32(ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)));
}

// ImGui 约定:label 里 "##xxx" 之后的内容只参与 ID 生成,渲染时要截掉。我们自家
// widget 全是手动 AddText,需要自己处理这件事,否则 preset0 / clr 这种 ID 后缀
// 会直接显示在 UI 上(用户报过 bug)。
static const char* RenderedTextEnd(const char* label) {
    const char* p = label;
    while (*p && !(p[0] == '#' && p[1] == '#')) ++p;
    return p;
}
static ImVec2 CalcRenderedTextSize(const char* label) {
    return ImGui::CalcTextSize(label, RenderedTextEnd(label));
}

// 根据 state 选当前的分类 emoji。返回的是指向 state 内部 char[] 的指针。
static const char* CategoryEmojiFromState(const State& s, util::AppCategory cat) {
    switch (cat) {
        case util::AppCategory::Game:    return s.emoji_game;
        case util::AppCategory::Browser: return s.emoji_browser;
        case util::AppCategory::Chat:    return s.emoji_chat;
        case util::AppCategory::Dev:     return s.emoji_dev;
        case util::AppCategory::Music:   return s.emoji_music;
        case util::AppCategory::Office:  return s.emoji_office;
        case util::AppCategory::Stream:  return s.emoji_stream;
        default:                         return util::DefaultCategoryEmoji(cat);
    }
}


std::string EffectiveStatusPrefix(const State& s) {
    // 1. status_override:文本非空 + 倒计时未到 0(或 clear_min == 0 永久)。
    bool override_active = (s.status_override[0] != 0) &&
                           (s.status_override_clear_min == 0 ||
                            s.status_override_remaining_sec > 0);
    if (override_active) {
        std::string out;
        if (s.status_override_emoji[0]) { out += s.status_override_emoji; out += ' '; }
        else                            { out += "\xF0\x9F\x93\x9D "; } // 默认 📝
        out += s.status_override;
        out += " \xC2\xB7 ";  // " · "
        return out;
    }
    // 2. AFK 自动检测。
    if (s.afk_auto && s.afk_threshold_min > 0 &&
        (int)s.idle_seconds >= s.afk_threshold_min * 60) {
        return std::string("\xF0\x9F\x92\xA4 AFK \xC2\xB7 ");
    }
    // 3. 前台应用(沿用 v1 行为,只在 toggle 开 + 有检测结果时挂)。
    if (s.show_foreground_app && s.foreground_app[0]) {
        std::string out;
        out += CategoryEmojiFromState(s, (util::AppCategory)s.foreground_category);
        out += ' ';
        out += s.foreground_app;
        out += " \xC2\xB7 ";
        return out;
    }
    return {};
}

namespace icons {
    static void DrawHome(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.5f;
        dl->AddTriangleFilled(ImVec2(c.x, c.y - s),
                              ImVec2(c.x - s, c.y - s * 0.05f),
                              ImVec2(c.x + s, c.y - s * 0.05f), col);
        dl->AddRectFilled(ImVec2(c.x - s * 0.7f, c.y - s * 0.05f),
                          ImVec2(c.x + s * 0.7f, c.y + s), col, S(1.5f));
    }
    static void DrawGear(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float ro = size * 0.5f, ri = size * 0.30f;
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            float a = (float)i * 6.2831853f / teeth, w = 0.18f;
            ImVec2 tip(c.x + std::cos(a) * ro, c.y + std::sin(a) * ro);
            ImVec2 a1(c.x + std::cos(a - w) * ri, c.y + std::sin(a - w) * ri);
            ImVec2 a2(c.x + std::cos(a + w) * ri, c.y + std::sin(a + w) * ri);
            dl->AddTriangleFilled(a1, tip, a2, col);
        }
        dl->AddCircleFilled(c, ri * 0.95f, col, 24);
        dl->AddCircleFilled(c, ri * 0.42f, U32(col::bg_sidebar), 16);
    }
    static void DrawSave(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.46f;
        dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, S(1.5f), 0, S(1.3f));
        dl->AddRectFilled(ImVec2(c.x - s + S(2.f), c.y - s),
                          ImVec2(c.x + s - S(4.f), c.y - s + s * 0.45f), col);
        dl->AddRect(ImVec2(c.x - s * 0.55f, c.y + s * 0.15f),
                    ImVec2(c.x + s * 0.55f, c.y + s - S(2.f)), col, S(1.f), 0, S(1.0f));
    }
    static void DrawClose(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.32f;
        dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, S(1.6f));
        dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, S(1.6f));
    }
    static void DrawMinimize(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.32f;
        dl->AddLine(ImVec2(c.x - s, c.y + s * 0.4f), ImVec2(c.x + s, c.y + s * 0.4f), col, S(1.6f));
    }
    static void DrawMusic(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.42f;
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.8f),
                    ImVec2(c.x + s * 0.6f, c.y - s),       col, S(1.8f));
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.8f),
                    ImVec2(c.x - s * 0.3f, c.y + s * 0.6f), col, S(1.6f));
        dl->AddLine(ImVec2(c.x + s * 0.6f, c.y - s),
                    ImVec2(c.x + s * 0.6f, c.y + s * 0.4f), col, S(1.6f));
        dl->AddCircleFilled(ImVec2(c.x - s * 0.5f, c.y + s * 0.6f), s * 0.3f, col, 12);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), s * 0.3f, col, 12);
    }
    static void DrawAppWindow(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.46f;
        dl->AddRect(ImVec2(c.x - s, c.y - s * 0.8f),
                    ImVec2(c.x + s, c.y + s * 0.6f),
                    col, S(2.f), 0, S(1.6f));
        dl->AddRectFilled(ImVec2(c.x - s + S(1.5f), c.y - s * 0.8f + S(1.5f)),
                          ImVec2(c.x + s - S(1.5f), c.y - s * 0.45f),
                          col, S(1.f));
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y + s * 0.85f),
                    ImVec2(c.x + s * 0.4f, c.y + s * 0.85f), col, S(1.6f));
        dl->AddLine(ImVec2(c.x, c.y + s * 0.6f),
                    ImVec2(c.x, c.y + s * 0.85f), col, S(1.6f));
    }
    static void DrawSpeaker(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.42f;
        // Box (cabinet)
        ImVec2 b0(c.x - s * 0.55f, c.y - s);
        ImVec2 b1(c.x + s * 0.05f, c.y + s);
        dl->AddRectFilled(b0, b1, col, S(1.5f));
        // Cone (triangle pointing right)
        dl->AddTriangleFilled(
            ImVec2(c.x + s * 0.05f, c.y - s * 0.55f),
            ImVec2(c.x + s * 0.05f, c.y + s * 0.55f),
            ImVec2(c.x + s * 0.7f,  c.y), col);
        // Two arc waves
        dl->PathArcTo(ImVec2(c.x + s * 0.2f, c.y), s * 0.55f, -0.6f, 0.6f, 12);
        dl->PathStroke(col, 0, S(1.4f));
        dl->PathArcTo(ImVec2(c.x + s * 0.2f, c.y), s * 0.85f, -0.6f, 0.6f, 14);
        dl->PathStroke(col, 0, S(1.4f));
    }
    static void DrawVideo(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float s = size * 0.46f;
        // Film/monitor body
        dl->AddRect(ImVec2(c.x - s, c.y - s * 0.7f),
                    ImVec2(c.x + s, c.y + s * 0.7f),
                    col, S(2.f), 0, S(1.6f));
        // Play triangle in the middle
        float pr = s * 0.34f;
        dl->AddTriangleFilled(
            ImVec2(c.x - pr * 0.4f, c.y - pr),
            ImVec2(c.x + pr * 0.7f, c.y),
            ImVec2(c.x - pr * 0.4f, c.y + pr), col);
    }
    // 聊天气泡 + sparkle:一个圆角矩形 + tail + 右上角四角星。
    static void DrawSparkChat(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float w = size * 0.42f, h = size * 0.34f;
        ImVec2 a(c.x - w, c.y - h * 1.05f);
        ImVec2 b(c.x + w * 0.55f, c.y + h * 0.35f);
        dl->AddRect(a, b, col, S(3.5f), 0, S(1.6f));
        // 气泡的尾巴(指向左下)
        ImVec2 t0(c.x - w * 0.2f, c.y + h * 0.35f);
        ImVec2 t1(c.x + w * 0.1f, c.y + h * 0.35f);
        ImVec2 t2(c.x - w * 0.5f, c.y + h * 0.95f);
        dl->AddTriangleFilled(t0, t1, t2, col);
        // 右上角的四角星 sparkle
        float sx = c.x + w * 0.65f, sy = c.y - h * 0.55f;
        float sr = h * 0.42f;
        dl->AddLine(ImVec2(sx, sy - sr), ImVec2(sx, sy + sr), col, S(1.5f));
        dl->AddLine(ImVec2(sx - sr, sy), ImVec2(sx + sr, sy), col, S(1.5f));
        float sd = sr * 0.55f;
        dl->AddLine(ImVec2(sx - sd, sy - sd), ImVec2(sx + sd, sy + sd), col, S(1.0f));
        dl->AddLine(ImVec2(sx + sd, sy - sd), ImVec2(sx - sd, sy + sd), col, S(1.0f));
    }
    static void DrawSun(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float r = size * 0.28f;
        dl->AddCircle(c, r, col, 24, S(1.6f));
        float ro = r + size * 0.10f;
        float rl = size * 0.16f;
        for (int i = 0; i < 8; ++i) {
            float a = i * (6.2831853f / 8.f);
            float ca = std::cos(a), sa = std::sin(a);
            dl->AddLine(ImVec2(c.x + ca * ro, c.y + sa * ro),
                        ImVec2(c.x + ca * (ro + rl), c.y + sa * (ro + rl)), col, S(1.6f));
        }
    }
    static void DrawMoon(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float r = size * 0.42f;
        dl->AddCircleFilled(c, r, col, 24);
        ImVec2 c2(c.x + r * 0.45f, c.y - r * 0.25f);
        dl->AddCircleFilled(c2, r * 0.85f, U32(col::bg_titlebar), 24);
    }
}

bool NLToggle(const char* label, bool* v) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGuiID id = win->GetID(label);
    const float row_h  = S(22.f);
    const float pill_w = S(28.f), pill_h = S(14.f);
    const float content_w = ImGui::GetContentRegionAvail().x;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImRect bb(cursor, ImVec2(cursor.x + content_w, cursor.y + row_h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *v = !*v;

    float t_on  = Anim(id, *v);
    float t_hov = Anim(win->GetID((const void*)((uintptr_t)id ^ 1u)), hovered);

    ImU32 txt = Mix(col::text_dim, col::text, ImMax(t_on, t_hov));
    const char* text_end = RenderedTextEnd(label);
    ImVec2 lsz = ImGui::CalcTextSize(label, text_end);
    win->DrawList->AddText(nullptr, 0.f,
        ImVec2(bb.Min.x, bb.Min.y + (row_h - lsz.y) * 0.5f), txt, label, text_end);

    ImVec2 pmin(bb.Max.x - pill_w, bb.Min.y + (row_h - pill_h) * 0.5f);
    ImVec2 pmax(pmin.x + pill_w, pmin.y + pill_h);
    win->DrawList->AddRectFilled(pmin, pmax, Mix(col::bg_input, col::accent, t_on), pill_h * 0.5f);

    float dr = pill_h * 0.5f - S(2.f);
    float dx = Lerp(pmin.x + dr + S(2.f), pmax.x - dr - S(2.f), t_on);
    float dy = (pmin.y + pmax.y) * 0.5f;
    win->DrawList->AddCircleFilled(ImVec2(dx, dy), dr, Mix(col::dot_off, col::dot_on, t_on), 16);
    return pressed;
}

bool NLSliderInt(const char* label, int* v, int v_min, int v_max) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGuiID id = win->GetID(label);
    const float row_h = S(34.f), track_h = S(3.f), knob_r = S(5.f);
    const float content_w = ImGui::GetContentRegionAvail().x;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImRect bb(cursor, ImVec2(cursor.x + content_w, cursor.y + row_h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *v);
    ImVec2 vsz = ImGui::CalcTextSize(buf);
    const char* label_end = RenderedTextEnd(label);
    win->DrawList->AddText(nullptr, 0.f, bb.Min, U32(col::text_dim), label, label_end);
    win->DrawList->AddText(ImVec2(bb.Max.x - vsz.x, bb.Min.y), U32(col::text), buf);

    float track_y = bb.Min.y + S(22.f);
    ImVec2 tmin(bb.Min.x, track_y), tmax(bb.Max.x, track_y + track_h);
    win->DrawList->AddRectFilled(tmin, tmax, U32(col::bg_input), track_h * 0.5f);

    float frac = (float)(*v - v_min) / (float)ImMax(1, v_max - v_min);
    frac = ImClamp(frac, 0.f, 1.f);
    ImVec2 fmax(Lerp(tmin.x, tmax.x, frac), tmax.y);
    win->DrawList->AddRectFilled(tmin, fmax, U32(col::accent), track_h * 0.5f);

    ImVec2 knob(fmax.x, (tmin.y + tmax.y) * 0.5f);
    win->DrawList->AddCircleFilled(knob, knob_r, U32(col::text), 18);

    bool hovered, held;
    ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (ImGui::IsItemActive()) {
        float mx = ImGui::GetIO().MousePos.x;
        float new_frac = ImClamp((mx - tmin.x) / ImMax(1.f, tmax.x - tmin.x), 0.f, 1.f);
        int new_v = v_min + (int)std::round(new_frac * (v_max - v_min));
        if (new_v != *v) { *v = new_v; return true; }
    }
    (void)hovered;
    return false;
}

void NLDivider() {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    win->DrawList->AddRectFilled(p, ImVec2(p.x + w, p.y + S(1.f)), U32(col::stroke));
    ImGui::Dummy(ImVec2(w, S(4.f)));
}

// ----------------------------------------------------------------------------
// NL 风格输入 / 下拉 / 按钮 —— 跟 NLToggle/NLSliderInt 一套视觉系统
// ----------------------------------------------------------------------------

// Chevron 下拉箭头 —— combo 右侧用,旋转 open 时翻转 180°
static void DrawChevron(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float t_open) {
    float s = size * 0.5f;
    // t_open 0→1 把箭头从 ▼ 旋成 ▲
    float dir = 1.f - 2.f * t_open;
    dl->AddLine(ImVec2(c.x - s * 0.6f, c.y - s * 0.18f * dir),
                ImVec2(c.x,             c.y + s * 0.32f * dir),
                col, S(1.6f));
    dl->AddLine(ImVec2(c.x + s * 0.6f, c.y - s * 0.18f * dir),
                ImVec2(c.x,             c.y + s * 0.32f * dir),
                col, S(1.6f));
}

// 单行输入。draw 路径:
//   1. push 一组透明 / 与 card 一致的 FrameBg style,让 ImGui::InputText 画的
//      底色跟我们想要的一致
//   2. ImGui::InputText 本身做编辑 + caret + IME
//   3. 拿 GetItemRect* 在底部画一条动画 underline(focused 时 accent,hover 时
//      dim 的 accent,空闲时透明)
// 这样能利用 ImGui 完整的文本编辑能力(IME 输入中文也不会丢字)。
bool NLInputText(const char* id, const char* hint,
                 char* buf, size_t buf_size, float width) {
    if (width == 0.f) width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col::bg_input);
    ImGui::PushStyleVar  (ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(7.f)));
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, S(6.f));

    bool changed = hint
        ? ImGui::InputTextWithHint(id, hint, buf, buf_size)
        : ImGui::InputText        (id,       buf, buf_size);

    ImVec2 r_min = ImGui::GetItemRectMin();
    ImVec2 r_max = ImGui::GetItemRectMax();
    bool   hov   = ImGui::IsItemHovered();
    bool   focused = ImGui::IsItemActive() || ImGui::IsItemFocused();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // Focus / idle 描边。亮色主题 idle 也画一圈 stroke,避免输入框融进白卡。
    ImGuiID anim_id = ImGui::GetCurrentWindow()->GetID((const void*)((uintptr_t)id ^ 0xA110u));
    float   t       = Anim(anim_id, focused, 16.f);
    float   alpha   = hov ? ImMax(t, 0.30f) : t;
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
        if (alpha > 0.005f) {
            dl->AddRect(r_min, r_max, U32(col::accent, alpha),
                        ImGui::GetStyle().FrameRounding, 0, S(1.5f));
        } else if (lightish) {
            dl->AddRect(r_min, r_max, U32(col::stroke, 0.95f),
                        ImGui::GetStyle().FrameRounding, 0, S(1.0f));
        }
    }
    return changed;
}

bool NLInputInt(const char* id, const char* hint, int* v,
                int v_min, int v_max, float width) {
    if (!v) return false;

    // 每个 id 一份编辑缓冲 + 上次同步出去的 int。编辑中不回写,失焦 / 回车再提交。
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID id_hash  = ImGui::GetID(id);
    ImGuiID key_sync = id_hash ^ 0xBEEFu;
    ImGuiID key_slot = id_hash ^ 0xCAFEu;

    // 用 StateStorage 存一个小 slot 索引,指向静态池里的 char 缓冲。
    // ImGuiStorage 只能存 int/float/void*,不能直接塞 16 字节字符串。
    struct PortBuf { char s[16]; int last_v; bool used; };
    static PortBuf pool[8]{};
    int slot = st->GetInt(key_slot, -1);
    if (slot < 0 || slot >= 8 || !pool[slot].used) {
        slot = -1;
        for (int i = 0; i < 8; ++i) if (!pool[i].used) { slot = i; break; }
        if (slot < 0) slot = 0; // 极端:复用 0
        pool[slot].used = true;
        pool[slot].last_v = *v + 1; // force initial sync
        st->SetInt(key_slot, slot);
    }
    PortBuf& pb = pool[slot];

    bool active = (ImGui::GetActiveID() == id_hash);
    // 外部改了 *v(读 config / 重置)且当前没在编辑 → 刷缓冲
    if (!active && pb.last_v != *v) {
        std::snprintf(pb.s, sizeof(pb.s), "%d", *v);
        pb.last_v = *v;
        st->SetInt(key_sync, *v);
    }

    if (width == 0.f) width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col::bg_input);
    ImGui::PushStyleVar  (ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(7.f)));
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, S(6.f));

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsDecimal |
                                ImGuiInputTextFlags_CharsNoBlank |
                                ImGuiInputTextFlags_EnterReturnsTrue;
    bool enter = hint
        ? ImGui::InputTextWithHint(id, hint, pb.s, sizeof(pb.s), flags)
        : ImGui::InputText        (id,       pb.s, sizeof(pb.s), flags);

    ImVec2 r_min = ImGui::GetItemRectMin();
    ImVec2 r_max = ImGui::GetItemRectMax();
    bool   hov   = ImGui::IsItemHovered();
    bool   focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    bool   deactivated = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ImGuiID anim_id = ImGui::GetCurrentWindow()->GetID((const void*)((uintptr_t)id ^ 0xA110u));
    float   t       = Anim(anim_id, focused, 16.f);
    float   alpha   = hov ? ImMax(t, 0.30f) : t;
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
        if (alpha > 0.005f) {
            dl->AddRect(r_min, r_max, U32(col::accent, alpha),
                        ImGui::GetStyle().FrameRounding, 0, S(1.5f));
        } else if (lightish) {
            dl->AddRect(r_min, r_max, U32(col::stroke, 0.95f),
                        ImGui::GetStyle().FrameRounding, 0, S(1.0f));
        }
    }

    // 回车或失焦提交
    if (enter || deactivated) {
        int parsed = 0;
        // 空串 / 非法 → 保持原值;合法则钳范围
        bool any_digit = false;
        for (const char* p = pb.s; *p; ++p) {
            if (*p >= '0' && *p <= '9') { any_digit = true; break; }
        }
        if (any_digit) {
            parsed = std::atoi(pb.s);
            if (parsed < v_min) parsed = v_min;
            if (parsed > v_max) parsed = v_max;
        } else {
            parsed = *v;
            if (parsed < v_min) parsed = v_min;
            if (parsed > v_max) parsed = v_max;
        }
        std::snprintf(pb.s, sizeof(pb.s), "%d", parsed);
        pb.last_v = parsed;
        if (parsed != *v) {
            *v = parsed;
            return true;
        }
    }
    return false;
}

bool NLInputTextMultiline(const char* id, const char* hint,
                          char* buf, size_t buf_size,
                          float width, float height,
                          int imgui_flags) {
    if (width == 0.f) width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col::bg_input);
    ImGui::PushStyleVar  (ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(7.f)));
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, S(6.f));

    bool changed = ImGui::InputTextMultiline(id, buf, buf_size,
                                              ImVec2(width, height),
                                              (ImGuiInputTextFlags)imgui_flags);
    ImVec2 r_min = ImGui::GetItemRectMin();
    ImVec2 r_max = ImGui::GetItemRectMax();
    bool   hov     = ImGui::IsItemHovered();
    bool   focused = ImGui::IsItemActive() || ImGui::IsItemFocused();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // hint placeholder —— 多行版没自带 hint
    if (hint && buf[0] == 0 && !focused) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(r_min.x + S(10.f), r_min.y + S(7.f)),
                    U32(col::text_dim, 0.7f), hint);
    }

    ImGuiID anim_id = ImGui::GetCurrentWindow()->GetID((const void*)((uintptr_t)id ^ 0xA111u));
    float   t       = Anim(anim_id, focused, 16.f);
    float   alpha   = hov ? ImMax(t, 0.30f) : t;
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
        if (alpha > 0.005f) {
            dl->AddRect(r_min, r_max, U32(col::accent, alpha),
                        ImGui::GetStyle().FrameRounding, 0, S(1.5f));
        } else if (lightish) {
            dl->AddRect(r_min, r_max, U32(col::stroke, 0.95f),
                        ImGui::GetStyle().FrameRounding, 0, S(1.0f));
        }
    }
    return changed;
}

// 自定义 Combo —— 不用 ImGui Popup 栈。
// 原因:Popup 的点外/Esc 自动关会和关合动画抢状态,导致"闪一下";
// 改成自绘浮层后,开合完全由我们控制:
//   - 再点触发框 → 关
//   - 点其它地方 → 关
//   - 选中一项 → 关
//   - 展开:slide + fade + 选项 staggered;关合:对称 ease,无闪烁
bool NLCombo(const char* id, int* current, const char* const* items, int count, float width) {
    if (!current || !items || count <= 0) return false;
    if (*current < 0 || *current >= count) *current = 0;

    if (width <= 0.f) width = ImGui::GetContentRegionAvail().x;
    const float height   = S(32.f);
    const float rounding = S(6.f);
    const float item_h   = S(30.f);
    const float pad_y    = S(6.f);
    const float gap      = S(2.f);
    const float popup_h  = pad_y * 2.f + item_h * (float)count + gap * (float)ImMax(0, count - 1);

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(id);
    ImGuiID btn_id = win->GetID("##btn");
    ImGuiStorage* st = ImGui::GetStateStorage();

    ImGuiID k_vis      = win->GetID("##open_vis");
    ImGuiID k_want     = win->GetID("##want_open");
    ImGuiID k_pill     = win->GetID("##pill_y");
    ImGuiID k_pill0    = win->GetID("##pill_init");
    ImGuiID k_ignore   = win->GetID("##ignore_out"); // 打开当帧忽略点外关闭

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImRect bb(origin, ImVec2(origin.x + width, origin.y + height));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, btn_id)) {
        ImGui::PopID();
        return false;
    }

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, btn_id, &hovered, &held);

    bool want_open = st->GetBool(k_want, false);
    if (pressed) {
        want_open = !want_open;
        st->SetBool(k_want, want_open);
        if (want_open) {
            st->SetFloat(k_vis, 0.f);
            st->SetBool(k_pill0, false);
            // 本帧鼠标还按着,别立刻被"点外"逻辑关掉
            st->SetBool(k_ignore, true);
        }
    }

    float open_vis = st->GetFloat(k_vis, 0.f);
    {
        float dst = want_open ? 1.f : 0.f;
        float speed = want_open ? 18.f : 16.f;
        float t = 1.f - std::exp(-speed * ImGui::GetIO().DeltaTime);
        open_vis = Lerp(open_vis, dst, t);
        if (std::fabs(open_vis - dst) < 1.f / 512.f) open_vis = dst;
        st->SetFloat(k_vis, open_vis);
    }

    // 触发框
    {
        ImDrawList* dl = win->DrawList;
        float t_hov = Anim(win->GetID("##hov"),
                           hovered || held || want_open || open_vis > 0.01f, 14.f);
        dl->AddRectFilled(bb.Min, bb.Max, Mix(col::bg_input, col::bg_hover, t_hov), rounding);

        float border_a = ImMax(open_vis, t_hov * 0.35f);
        // 亮色 idle 描边更实,避免下拉框融进白卡
        const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
        if (border_a > 0.005f)
            dl->AddRect(bb.Min, bb.Max, U32(col::accent, border_a), rounding, 0, S(1.5f));
        else
            dl->AddRect(bb.Min, bb.Max, U32(col::stroke, lightish ? 0.95f : 0.55f),
                        rounding, 0, S(1.0f));

        const char* preview = items[*current] ? items[*current] : "";
        ImVec2 tsz = ImGui::CalcTextSize(preview);
        dl->PushClipRect(bb.Min, ImVec2(bb.Max.x - S(28.f), bb.Max.y), true);
        dl->AddText(ImVec2(bb.Min.x + S(10.f), bb.Min.y + (height - tsz.y) * 0.5f),
                    U32(col::text), preview);
        dl->PopClipRect();

        DrawChevron(dl, ImVec2(bb.Max.x - S(14.f), (bb.Min.y + bb.Max.y) * 0.5f),
                    S(14.f), U32(col::text_dim), open_vis);
    }

    bool changed = false;
    const bool panel_alive = (want_open || open_vis > 0.001f);
    if (!panel_alive) {
        ImGui::PopID();
        return false;
    }

    const float ease  = EaseOutCubic(open_vis);
    const float slide = (1.f - ease) * S(8.f);
    const float alpha = ease;
    const ImVec2 panel_pos(bb.Min.x, bb.Max.y + S(4.f) - slide);
    const ImVec2 panel_sz(width, popup_h);
    const ImRect panel_bb(panel_pos, ImVec2(panel_pos.x + panel_sz.x, panel_pos.y + panel_sz.y));

    // ---- 全屏透明挡板:吃掉点外点击,不抢触发框 / 面板上的点击 ----
    // 用独立窗口,避免被 content clip;NoInputs 关掉,自己用 InvisibleButton。
    {
        char catcher_name[64];
        std::snprintf(catcher_name, sizeof(catcher_name), "##nlcombo_catcher_%08X", btn_id);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGuiWindowFlags cflags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin(catcher_name, nullptr, cflags)) {
            ImGui::InvisibleButton("##catch", ImGui::GetIO().DisplaySize);
            bool ignore = st->GetBool(k_ignore, false);
            if (ignore && !ImGui::GetIO().MouseDown[0]) {
                // 打开时那次按下松开后再允许点外关闭
                st->SetBool(k_ignore, false);
                ignore = false;
            }
            if (!ignore && want_open && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                // 点在触发框或面板上 → 放行(触发框自己 toggle;面板自己选)
                if (!bb.Contains(mp) && !panel_bb.Contains(mp)) {
                    want_open = false;
                    st->SetBool(k_want, false);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ---- 下拉面板(普通窗口,不是 popup,无 Esc 自动关) ----
    {
        char panel_name[64];
        std::snprintf(panel_name, sizeof(panel_name), "##nlcombo_panel_%08X", btn_id);

        ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(panel_sz, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(ImClamp(alpha, 0.f, 1.f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_input);
        ImGui::PushStyleColor(ImGuiCol_Border,   col::stroke);
        ImGui::PushStyleColor(ImGuiCol_Text,     col::text);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   S(8.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(S(4.f), pad_y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(0.f, gap));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,            ImClamp(alpha, 0.f, 1.f));

        ImGuiWindowFlags pflags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin(panel_name, nullptr, pflags)) {
            // 保证盖在 content / catcher 之上
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

            ImDrawList* pdl = ImGui::GetWindowDrawList();
            ImVec2 content0 = ImGui::GetCursorScreenPos();
            float pill_target = (float)(*current) * (item_h + gap) + item_h * 0.5f;

            // 交互只在基本展开完后启用,关合过程中不接点击
            const bool interactive = want_open && open_vis > 0.90f;

            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                bool sel = (i == *current);
                const char* label = items[i] ? items[i] : "";

                float item_start = (float)i * 0.03f;
                float item_t = (open_vis - item_start) / 0.20f;
                if (item_t < 0.f) item_t = 0.f;
                if (item_t > 1.f) item_t = 1.f;
                float item_ease = EaseOutCubic(item_t);
                float item_dx   = (1.f - item_ease) * S(6.f);
                float item_a    = item_ease * alpha;

                ImVec2 row_min = ImGui::GetCursorScreenPos();
                ImVec2 row_sz(width - S(8.f), item_h);
                if (interactive) ImGui::InvisibleButton("##row", row_sz);
                else             ImGui::Dummy(row_sz);

                bool row_hov = interactive && ImGui::IsItemHovered();
                bool row_clk = interactive && ImGui::IsItemClicked();

                ImVec2 row_max(row_min.x + row_sz.x, row_min.y + row_sz.y);
                if (sel) pill_target = (row_min.y + row_max.y) * 0.5f - content0.y;

                if ((sel || row_hov) && item_a > 0.01f) {
                    ImU32 fill = sel ? U32(col::accent, (row_hov ? 0.28f : 0.16f) * item_a)
                                     : U32(col::bg_hover, 0.95f * item_a);
                    pdl->AddRectFilled(ImVec2(row_min.x + item_dx, row_min.y),
                                       row_max, fill, S(5.f));
                }

                ImVec2 lsz = ImGui::CalcTextSize(label);
                pdl->AddText(ImVec2(row_min.x + S(12.f) + item_dx,
                                    row_min.y + (item_h - lsz.y) * 0.5f),
                             sel ? U32(col::accent, item_a) : U32(col::text, item_a),
                             label);

                if (row_clk) {
                    if (*current != i) {
                        *current = i;
                        changed = true;
                    }
                    want_open = false;
                    st->SetBool(k_want, false);
                }
                ImGui::PopID();
            }

            // 左侧滑动 pill
            {
                bool pill_inited = st->GetBool(k_pill0, false);
                float pill_y = st->GetFloat(k_pill, pill_target);
                if (!pill_inited) {
                    pill_y = pill_target;
                    st->SetBool(k_pill0, true);
                } else {
                    float pt = 1.f - std::exp(-18.f * ImGui::GetIO().DeltaTime);
                    pill_y = Lerp(pill_y, pill_target, pt);
                    if (std::fabs(pill_y - pill_target) < 0.25f) pill_y = pill_target;
                }
                st->SetFloat(k_pill, pill_y);

                float pill_hh = item_h * 0.55f;
                float px = content0.x + S(2.f);
                float py = content0.y + pill_y - pill_hh * 0.5f;
                pdl->AddRectFilled(ImVec2(px, py),
                                   ImVec2(px + S(3.f), py + pill_hh),
                                   U32(col::accent, alpha), S(1.5f));
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(5);
        ImGui::PopStyleColor(3);
    }

    ImGui::PopID();
    return changed;
}

// NL Button —— hover scale + press 下沉 + accent halo。
// accent=true 用主色;danger=true 优先用红色;否则用 input 配色。
// disabled=true 灰一档且点击无效。
bool NLButton(const char* label, float width, float height,
              bool accent, bool danger, bool disabled) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    if (width == 0.f)  width  = ImGui::GetContentRegionAvail().x;
    if (height == 0.f) height = S(36.f);

    ImGuiID id = win->GetID(label);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImRect bb(origin, ImVec2(origin.x + width, origin.y + height));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = false;
    if (!disabled) {
        pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    } else {
        // 仍画出 disabled 的视觉效果,但 ButtonBehavior 不调
        hovered = false;
        held    = false;
    }

    // 动画 state
    float t_hov   = Anim(win->GetID((const void*)((uintptr_t)id ^ 0xA001u)), hovered);
    float t_press = Anim(win->GetID((const void*)((uintptr_t)id ^ 0xA002u)), held, 22.f);
    // halo:松开瞬间从 1 渐衰 0,衰减时间 350ms
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID halo_id  = win->GetID((const void*)((uintptr_t)id ^ 0xA003u));
    float halo = st->GetFloat(halo_id, 0.f);
    if (pressed) halo = 1.f;
    halo -= ImGui::GetIO().DeltaTime / 0.35f;
    if (halo < 0.f) halo = 0.f;
    st->SetFloat(halo_id, halo);

    // 按下时盒子下沉 1px,模拟物理点击
    float dy = t_press * S(1.f);
    ImVec2 bmin(bb.Min.x, bb.Min.y + dy);
    ImVec2 bmax(bb.Max.x, bb.Max.y + dy);

    // 配色
    ImVec4 base_bg, hover_bg, text_col;
    if (disabled) {
        base_bg  = ImVec4(0.30f, 0.34f, 0.40f, 1.f);
        hover_bg = base_bg;
        text_col = ImVec4(0.55f, 0.60f, 0.66f, 1.f);
    } else if (danger) {
        base_bg  = ImVec4(0.65f, 0.25f, 0.30f, 1.f);
        hover_bg = ImVec4(0.78f, 0.30f, 0.35f, 1.f);
        text_col = ImVec4(0.05f, 0.07f, 0.10f, 1.f);
    } else if (accent) {
        base_bg  = col::accent;
        hover_bg = ImVec4(col::accent.x * 1.15f, col::accent.y * 1.15f, col::accent.z * 1.15f, 1.f);
        text_col = ImVec4(0.05f, 0.07f, 0.10f, 1.f);
    } else {
        base_bg  = col::bg_input;
        hover_bg = col::bg_hover;
        text_col = col::text;
    }
    ImU32 bg_u32 = Mix(base_bg, hover_bg, t_hov);

    // halo glow(只 accent / danger 显眼,普通按钮关掉避免视觉污染)
    if (halo > 0.005f && (accent || danger)) {
        float p_inv = 1.f - halo;
        float ease  = 1.f - p_inv * p_inv * p_inv;
        float halo_extra = S(7.f) * ease;
        ImVec4 glow = (danger ? ImVec4(0.85f, 0.30f, 0.35f, 0.f) : col::accent);
        glow.w = ease * 0.45f;
        win->DrawList->AddRectFilled(
            ImVec2(bmin.x - halo_extra, bmin.y - halo_extra),
            ImVec2(bmax.x + halo_extra, bmax.y + halo_extra),
            ImGui::ColorConvertFloat4ToU32(glow), S(10.f));
    }

    win->DrawList->AddRectFilled(bmin, bmax, bg_u32, S(6.f));

    // hover 边亮一圈(非 accent 才显)
    if (!accent && !danger && !disabled && t_hov > 0.01f) {
        ImVec4 stroke_c = col::accent; stroke_c.w = t_hov * 0.55f;
        win->DrawList->AddRect(bmin, bmax, ImGui::ColorConvertFloat4ToU32(stroke_c),
                               S(6.f), 0, S(1.f));
    }

    const char* label_end = RenderedTextEnd(label);
    ImVec2 lsz = ImGui::CalcTextSize(label, label_end);
    ImVec2 lpos((bmin.x + bmax.x - lsz.x) * 0.5f, (bmin.y + bmax.y - lsz.y) * 0.5f);
    win->DrawList->AddText(nullptr, 0.f, lpos, U32(text_col), label, label_end);
    return pressed && !disabled;
}

static void SectionTitle(const char* upper_title) {
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::PushFont(font_caption);

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 标题 caps,后面接 hairline rule 一直拉到内容区右沿。
    dl->AddText(p, U32(col::text_caption), upper_title);
    ImVec2 title_sz = ImGui::CalcTextSize(upper_title);

    float rule_x = p.x + title_sz.x + S(10.f);
    float rule_right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x
                       - ImGui::GetStyle().WindowPadding.x;
    float rule_y = p.y + title_sz.y * 0.5f - S(0.5f);
    if (rule_right > rule_x + S(8.f)) {
        dl->AddRectFilled(ImVec2(rule_x, rule_y),
                          ImVec2(rule_right, rule_y + S(1.f)),
                          U32(col::stroke));
    }

    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(12.f)));
}

// 用 ImDrawList 的通道切分实现卡片背景,不用 BeginChild —— child window 在
// 这个版本的 ImGui 上会拦截鼠标事件导致按钮点不动(踩过这个坑)。
static ImVec2 g_card_start;
// 卡片序号:DrawXxx 在每帧 / 每次 tab 切换时 reset,CardBegin 自增。
// 用来给 staggered slide-in 加索引,卡片越靠下出现得越晚。
static int    g_card_index = 0;
// 跨函数共享的"tab 过渡进度",由 Draw() 在每帧设好,CardBegin 读到来算自家的
// stagger 子进度。
static float  g_tab_anim_t = 1.f;
// 单卡片当前的 X 缩进(stagger 用),CardEnd 用它来 Unindent 对消。
static float  g_card_extra_indent = 0.f;

static void CardBegin(const char* /*id*/) {
    // staggered slide-in:每张卡片在 tab 过渡里有一段 180ms 的子窗口,
    // 卡片越靠后开始得越晚,横向偏移 + 自身 alpha 渐入。
    float start = (float)g_card_index * 0.07f;
    float local = (g_tab_anim_t - start) / 0.22f;
    if (local < 0.f) local = 0.f;
    if (local > 1.f) local = 1.f;
    float u    = 1.f - local;
    float ease = 1.f - u * u * u;          // ease-out cubic
    g_card_extra_indent = (1.f - ease) * S(18.f);
    if (g_card_extra_indent > 0.01f) ImGui::Indent(g_card_extra_indent);

    g_card_start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);  // content channel
    ImGui::Indent(S(14.f));
    ImGui::Dummy(ImVec2(0, S(8.f)));
}

static void CardEnd() {
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::Unindent(S(14.f));

    ImVec2 r_min = g_card_start;
    float  r_right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x
                     - ImGui::GetStyle().WindowPadding.x;
    ImVec2 r_max(r_right, ImGui::GetCursorScreenPos().y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    // 亮色主题下给卡片一点极轻的投影 + 描边,否则白卡贴在浅灰底上看不出层次。
    // 暗色保持纯填充(阴影会脏)。用 stroke 的亮度粗判当前主题。
    const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
    if (lightish) {
        // 两层 soft shadow,向下偏 1–2px
        dl->AddRectFilled(ImVec2(r_min.x, r_min.y + S(2.f)),
                          ImVec2(r_max.x, r_max.y + S(2.f)),
                          U32(ImVec4(0.10f, 0.14f, 0.20f, 0.04f)), S(8.f));
        dl->AddRectFilled(ImVec2(r_min.x, r_min.y + S(1.f)),
                          ImVec2(r_max.x, r_max.y + S(1.f)),
                          U32(ImVec4(0.10f, 0.14f, 0.20f, 0.06f)), S(8.f));
    }
    dl->AddRectFilled(r_min, r_max, U32(col::bg_card), S(8.f));
    if (lightish) {
        dl->AddRect(r_min, r_max, U32(col::stroke, 0.90f), S(8.f), 0, S(1.0f));
    }
    dl->ChannelsMerge();

    if (g_card_extra_indent > 0.01f) ImGui::Unindent(g_card_extra_indent);
    g_card_extra_indent = 0.f;

    ImGui::Dummy(ImVec2(0, S(10.f)));
    g_card_index++;
}

// 状态点(● / ○ 的替代)。
// active=true 时画一颗充满 + 呼吸 pulse;false 时空心暗淡。
// 调用方需要自己安排好 cursor 位置 —— 此函数只画,不动 cursor。
// 返回值是 dot 的右边沿 x,用来后接 label 文本。
static float DrawStatusDot(ImVec2 origin, float radius, bool active, ImU32 col_on, ImU32 col_off) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(origin.x + radius, origin.y + radius);
    if (active) {
        // 呼吸 pulse:用 ImGui 的时间累计做正弦,1.4s 一个周期
        double t_now = ImGui::GetTime();
        float pulse = 0.5f + 0.5f * (float)std::sin(t_now * 4.488f);  // 2π/1.4
        // 外环 halo
        ImVec4 halo = ImGui::ColorConvertU32ToFloat4(col_on);
        halo.w *= 0.18f + 0.22f * pulse;
        dl->AddCircleFilled(c, radius * (1.55f + pulse * 0.25f),
                            ImGui::ColorConvertFloat4ToU32(halo), 20);
        dl->AddCircleFilled(c, radius, col_on, 16);
    } else {
        dl->AddCircle(c, radius, col_off, 16, S(1.4f));
    }
    return c.x + radius;
}

// 渲染一行 "[●/○] label" 文本,带 dot pulse。row 的高度等于当前 font line height。
static void StatusRow(bool active, const char* label, ImVec4 active_color, ImVec4 inactive_color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float dot_r = S(4.f);
    float text_h = ImGui::GetTextLineHeight();
    DrawStatusDot(ImVec2(p.x, p.y + (text_h * 0.5f - dot_r)), dot_r,
                  active, U32(active_color), U32(inactive_color));
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(p.x + dot_r * 2.f + S(6.f), p.y),
        active ? U32(active_color) : U32(inactive_color), label);
    ImGui::Dummy(ImVec2(0, text_h));
}

// 进度条:fraction 走低通滤波,看起来有惯性。每帧调一次,storage_key 必须稳定。
static float AnimatedFraction(ImGuiID storage_key, float target) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float cur = st->GetFloat(storage_key, target);
    float dt  = ImGui::GetIO().DeltaTime;
    if (dt > 0.05f) dt = 0.05f;
    // 跟随系数 10/sec,大跳的时候 ~300ms 跟上
    cur += (target - cur) * (1.f - std::exp(-10.f * dt));
    if (std::fabs(target - cur) < 1.f / 2048.f) cur = target;
    st->SetFloat(storage_key, cur);
    return cur;
}

static void DrawTitleBarContent(State& s, int win_w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = S(40.f);

    ImGui::PushFont(font_logo);
    const char* logo = "VRC LYRICS";
    ImVec2 sz = ImGui::CalcTextSize(logo);
    dl->AddText(ImVec2(S(20.f), (h - sz.y) * 0.5f), U32(col::text), logo);
    ImGui::PopFont();

    if (s.save_toast_sec > 0.f) {
        s.save_toast_sec -= ImGui::GetIO().DeltaTime;
        float alpha = ImMin(1.f, s.save_toast_sec * 2.f);
        ImGui::PushFont(font_body);
        const char* msg = i18n::t("Saved", "已保存", "已儲存");
        ImVec2 msz = ImGui::CalcTextSize(msg);
        ImVec2 mp(S(20.f) + sz.x + S(14.f), (h - msz.y) * 0.5f);
        ImVec4 pill_col = col::accent; pill_col.w = alpha * 0.85f;
        dl->AddRectFilled(ImVec2(mp.x - S(8.f), mp.y - S(3.f)),
                          ImVec2(mp.x + msz.x + S(8.f), mp.y + msz.y + S(3.f)),
                          ImGui::ColorConvertFloat4ToU32(pill_col), S(8.f));
        dl->AddText(mp, U32(ImVec4(0.05f, 0.07f, 0.10f, alpha)), msg);
        ImGui::PopFont();
    }

    struct IconBtn { const char* id; void(*draw)(ImDrawList*, ImVec2, float, ImU32); };
    IconBtn fns[] = {
        { "##i_save",  icons::DrawSave },
        { "##i_theme", s.theme == Theme::Dark ? icons::DrawSun : icons::DrawMoon },
    };
    const float btn_size = S(28.f);
    const float gap = S(2.f);
    float right = (float)win_w - S(12.f);

    float close_x = right - btn_size;
    float min_x   = close_x - btn_size - gap;
    float fn_right = min_x - S(14.f);

    ImGui::SetCursorScreenPos(ImVec2(close_x, (h - btn_size) * 0.5f));
    bool clicked_close = ImGui::InvisibleButton("##close", ImVec2(btn_size, btn_size));
    bool hov_close = ImGui::IsItemHovered();
    if (hov_close)
        dl->AddRectFilled(ImVec2(close_x, (h - btn_size) * 0.5f),
                          ImVec2(close_x + btn_size, (h + btn_size) * 0.5f),
                          U32(ImVec4(0.85f, 0.25f, 0.30f, 0.9f)), S(4.f));
    icons::DrawClose(dl, ImVec2(close_x + btn_size * 0.5f, h * 0.5f), S(16.f),
                     U32(hov_close ? col::text : col::text_dim));

    ImGui::SetCursorScreenPos(ImVec2(min_x, (h - btn_size) * 0.5f));
    bool clicked_min = ImGui::InvisibleButton("##min", ImVec2(btn_size, btn_size));
    bool hov_min = ImGui::IsItemHovered();
    if (hov_min)
        dl->AddRectFilled(ImVec2(min_x, (h - btn_size) * 0.5f),
                          ImVec2(min_x + btn_size, (h + btn_size) * 0.5f),
                          U32(col::bg_hover), S(4.f));
    icons::DrawMinimize(dl, ImVec2(min_x + btn_size * 0.5f, h * 0.5f), S(16.f),
                        U32(hov_min ? col::text : col::text_dim));

    float ix = fn_right - btn_size;
    for (int i = (int)IM_ARRAYSIZE(fns) - 1; i >= 0; --i, ix -= btn_size + gap) {
        ImGui::SetCursorScreenPos(ImVec2(ix, (h - btn_size) * 0.5f));
        bool fn_click = ImGui::InvisibleButton(fns[i].id, ImVec2(btn_size, btn_size));
        bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ImVec2(ix, (h - btn_size) * 0.5f),
                              ImVec2(ix + btn_size, (h + btn_size) * 0.5f),
                              U32(col::bg_hover), S(4.f));
        fns[i].draw(dl, ImVec2(ix + btn_size * 0.5f, h * 0.5f), S(14.f),
                    U32(hov ? col::text : col::text_dim));
        if (fn_click && std::strcmp(fns[i].id, "##i_save") == 0) {
            s.save_request = true;
        }
        if (fn_click && std::strcmp(fns[i].id, "##i_theme") == 0) {
            Theme prev = s.theme;
            s.theme = (s.theme == Theme::Dark) ? Theme::Light : Theme::Dark;
            BeginThemeTransition(prev, s.theme);
        }
    }

    if (clicked_close) {
        HWND hw = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
        if (!hw) hw = GetForegroundWindow();
        PostMessage(hw, WM_CLOSE, 0, 0);
    }
    if (clicked_min) {
        HWND hw = (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
        if (!hw) hw = GetForegroundWindow();
        ShowWindow(hw, SW_MINIMIZE);
    }

    dl->AddRectFilled(ImVec2(0.f, h), ImVec2((float)win_w, h + S(1.f)), U32(col::stroke));
}

static float g_sidebar_tab_ys[8] = {};
static int   g_sidebar_tab_count = 0;
static float g_sidebar_indicator_y = -1.f;  // 已动画到的 Y(始终是当前帧的渲染值)

static bool SidebarTab(const char* label, void(*icon)(ImDrawList*, ImVec2, float, ImU32), bool selected) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGuiID id = win->GetID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(34.f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImRect bb(p, ImVec2(p.x + w, p.y + h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    // 记录这个 tab 的 Y,后面统一画一根 sliding indicator。
    if (g_sidebar_tab_count < (int)IM_ARRAYSIZE(g_sidebar_tab_ys)) {
        g_sidebar_tab_ys[g_sidebar_tab_count++] = bb.Min.y;
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    float t_hov = Anim(win->GetID((const void*)((uintptr_t)id ^ 2u)), hovered || selected);

    // 亮色 sidebar 上选中/hover 需要更实的底,否则几乎看不见。
    const bool lightish = (col::bg_card.x + col::bg_card.y + col::bg_card.z) > 2.0f;
    if (selected) {
        win->DrawList->AddRectFilled(bb.Min, bb.Max,
            U32(col::bg_hover, lightish ? 0.95f : 0.55f), 0.f);
    } else if (t_hov > 0.01f) {
        win->DrawList->AddRectFilled(bb.Min, bb.Max,
            U32(col::bg_hover, t_hov * (lightish ? 0.75f : 0.45f)), 0.f);
    }

    ImU32 fg = Mix(col::text_dim, col::text, selected ? 1.f : t_hov * 0.6f);
    icon(win->DrawList, ImVec2(bb.Min.x + S(22.f), (bb.Min.y + bb.Max.y) * 0.5f), S(16.f), fg);

    ImGui::PushFont(font_body);
    const char* label_end = RenderedTextEnd(label);
    ImVec2 lsz = ImGui::CalcTextSize(label, label_end);
    win->DrawList->AddText(nullptr, 0.f,
        ImVec2(bb.Min.x + S(40.f), (bb.Min.y + bb.Max.y - lsz.y) * 0.5f),
        fg, label, label_end);
    ImGui::PopFont();
    return pressed;
}

// 在 sidebar 所有 tab 都绘制完之后调,统一画一个会滑动的 accent 高亮条。
static void DrawSidebarIndicator(int current_tab_idx) {
    if (g_sidebar_tab_count == 0) return;
    if (current_tab_idx < 0) current_tab_idx = 0;
    if (current_tab_idx >= g_sidebar_tab_count) current_tab_idx = g_sidebar_tab_count - 1;

    float target_y = g_sidebar_tab_ys[current_tab_idx];

    // 第一帧初始化:别让它从 0 滑过来,直接 snap 到位。
    if (g_sidebar_indicator_y < 0.f) g_sidebar_indicator_y = target_y;

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.05f) dt = 0.05f;
    g_sidebar_indicator_y += (target_y - g_sidebar_indicator_y) * (1.f - std::exp(-18.f * dt));
    if (std::fabs(target_y - g_sidebar_indicator_y) < 0.5f) g_sidebar_indicator_y = target_y;

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    float h = S(34.f);
    win->DrawList->AddRectFilled(
        ImVec2(win->Pos.x, g_sidebar_indicator_y + S(8.f)),
        ImVec2(win->Pos.x + S(3.f), g_sidebar_indicator_y + h - S(8.f)),
        U32(col::accent), S(1.5f));
}

static void DrawLyrics(State& s) {
    SectionTitle(i18n::t("NOW PLAYING", "正在播放", "正在播放"));
    CardBegin("##card_np");
    ImGui::PushFont(font_title);
    ImGui::TextColored(s.np_detected ? col::text : col::text_dim, "%s",
                       s.np_detected ? s.np_title
                                     : i18n::t("not detected", "未检测到", "未偵測到"));
    ImGui::PopFont();
    ImGui::PushFont(font_body);
    if (s.np_detected) {
        ImGui::TextColored(col::text_dim, "%s%s%s",
                           s.np_artist,
                           (s.np_artist[0] && s.np_album[0]) ? " · " : "",
                           s.np_album);
        const char* src_label =
            s.np_source == 1 ? "NetEase Cloud" :
            s.np_source == 2 ? "Spotify"       :
            s.np_source == 3 ? "YouTube Music" : "Other";
        if (s.np_source == 1) {
            ImGui::TextColored(col::text_dim, "%s · %s %s",
                               src_label,
                               i18n::t("Track ID:", "曲目 ID:", "曲目 ID:"),
                               s.np_ncm_id[0] ? s.np_ncm_id : "-");
        } else {
            ImGui::TextColored(col::text_dim, "%s %s",
                               i18n::t("Source:", "来源:", "來源:"),
                               src_label);
        }
        int p = s.np_pos_ms / 1000, d = s.np_dur_ms / 1000;
        ImGui::TextColored(col::text_dim, "%02d:%02d / %02d:%02d  (%s)",
                           p / 60, p % 60, d / 60, d % 60,
                           s.np_playing
                             ? i18n::t("playing", "播放中", "播放中")
                             : i18n::t("paused",  "已暂停", "已暫停"));
        if (s.np_has_lyrics && s.np_current_line[0]) {
            ImGui::Dummy(ImVec2(0, S(4.f)));
            ImGui::PushFont(font_medium);
            ImGui::TextColored(col::accent, "\xF0\x9F\x8E\xA4 %s", s.np_current_line);
            ImGui::PopFont();
        } else if (s.np_has_lyrics) {
            ImGui::TextColored(col::text_dim, "%s",
                i18n::t("(instrumental section)", "(纯音乐段落)", "(純音樂段落)"));
        } else if (s.np_ncm_id[0]) {
            ImGui::TextColored(col::text_dim, "%s",
                i18n::t("(no lyrics available)", "(暂无歌词)", "(暫無歌詞)"));
        }
    } else {
        ImGui::TextColored(col::text_dim, "Artist · Album");
        ImGui::TextColored(col::text_dim, "Source: -");
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("Open NetEase / Spotify / YouTube Music. NetEase needs inflink-rs for direct ID match.",
                    "打开网易云 / Spotify / YouTube Music。网易云装 inflink-rs 插件可直接按 ID 查",
                    "開啟網易雲 / Spotify / YouTube Music。網易雲裝 inflink-rs 外掛可直接按 ID 查"));
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, S(8.f)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - S(14.f);  // match left indent — inset progress bar inside the card
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(4.f)), U32(col::bg_input), S(2.f));
    float frac = s.np_dur_ms > 0 ? (float)s.np_pos_ms / (float)s.np_dur_ms : 0.f;
    frac = frac < 0.f ? 0.f : (frac > 1.f ? 1.f : frac);
    if (frac > 0.f) {
        dl->AddRectFilled(p, ImVec2(p.x + w * frac, p.y + S(4.f)), U32(col::accent), S(2.f));
    }
    ImGui::Dummy(ImVec2(w, S(6.f)));
    CardEnd();

    SectionTitle(i18n::t("SERVICE", "服务", "服務"));
    CardBegin("##card_svc");
    const char* btn_label = s.service_running
        ? i18n::t("Stop", "停止", "停止")
        : i18n::t("Start", "启动", "啟動");
    float btn_w = ImGui::GetContentRegionAvail().x - S(14.f);
    ImGui::PushFont(font_medium);
    if (NLButton(btn_label, btn_w, S(36.f),
                 /*accent*/!s.service_running, /*danger*/s.service_running)) {
        s.service_running = !s.service_running;
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, S(4.f)));
    NLToggle(i18n::t("Send while paused", "暂停时仍发送", "暫停時仍傳送"), &s.send_while_paused);
    CardEnd();
}

// 滑动 emoji 选择器:4 个候选,选中态用一条 accent pill 显示。切换时:
//   - pill 走二阶弹簧物理(stiffness/damping),~350ms 弹到位,带一点点 overshoot
//   - 速度越快 pill 横向"拉长"(squash-stretch),停下来弹回标准宽度
//   - 点击瞬间外加一圈 ease-out cubic 衰减的 halo
// 返回 true 表示选择被改了。状态全部存在 ImGuiStorage 里,跨帧持久。
//
// !!! 重要 !!!
// 所有 ID 必须用 slot 的【内存地址】当 cookie,不能用字符串内容。
// 因为 slot 内容会被用户点击改写,如果用 GetID(const char*) 把内容当字符串哈希,
// 每次切换 emoji 都会换一套新 ID,storage 里的 pos/vel 拿不回来 —— 动画就丢了。
static bool AnimatedEmojiPicker(char* slot, size_t slot_size,
                                const char* const choices[4]) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    // 当前选中索引:与 slot 字符串一致的候选,没匹配上就当 0。
    int selected = 0;
    for (int i = 0; i < 4; ++i) {
        if (std::strcmp(slot, choices[i]) == 0) { selected = i; break; }
    }

    // 用 slot 的内存地址做基址,异或两个常量出 3 个稳定 ID。
    ImGuiStorage* st = ImGui::GetStateStorage();
    uintptr_t base   = (uintptr_t)slot;
    ImGuiID anim_id  = win->GetID((const void*)base);
    ImGuiID vel_id   = win->GetID((const void*)(base ^ (uintptr_t)0xCAFEu));
    ImGuiID pulse_id = win->GetID((const void*)(base ^ (uintptr_t)0xBEEFu));

    // 钳一下 dt,主线程偶尔被卡(切窗口/暂停)时一帧 0.3s 会让弹簧爆炸。
    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.05f) dt = 0.05f;

    // 二阶弹簧:acc = k * (target - pos) - c * vel。
    // k=280, c=22 → 临界阻尼 c_crit = 2*sqrt(280) ≈ 33.5,这里 c/c_crit ≈ 0.66,
    // 略欠阻尼,带轻微 overshoot,iOS 风格。完整 settle ~350ms。
    float pos = st->GetFloat(anim_id, (float)selected);
    float vel = st->GetFloat(vel_id,  0.f);
    const float k_spring = 280.f;
    const float c_damp   = 22.f;
    float target = (float)selected;
    float acc = k_spring * (target - pos) - c_damp * vel;
    vel += acc * dt;
    pos += vel * dt;
    // 接近静止时强制对齐,免得永远在小数位震荡。
    if (std::fabs(target - pos) < 1.f / 1024.f && std::fabs(vel) < 0.02f) {
        pos = target;
        vel = 0.f;
    }
    st->SetFloat(anim_id, pos);
    st->SetFloat(vel_id,  vel);

    // Halo pulse:点击 1.f → 0,400ms 线性衰减,显示时再做 ease-out cubic
    float pulse_raw = st->GetFloat(pulse_id, 0.f);
    pulse_raw = pulse_raw - dt / 0.40f;
    if (pulse_raw < 0.f) pulse_raw = 0.f;
    st->SetFloat(pulse_id, pulse_raw);

    const float btn_w = S(36.f);
    const float btn_h = S(26.f);
    const float gap   = S(2.f);
    const float pitch = btn_w + gap;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = win->DrawList;

    // squash-stretch:速度大时 pill 横向拉长(对称扩张就够了,非对称版要两条弹簧)。
    // vel 单位 cells/sec,典型峰值 6-12,系数 0.04 让顶峰拉伸 25% 左右。
    float vel_abs = std::fabs(vel);
    float stretch = vel_abs * 0.04f;
    if (stretch > 0.25f) stretch = 0.25f;
    float extra_w = btn_w * stretch;

    // Pill 主体位置(基于动画位置 pos,而非 selected)
    ImVec2 pill_min(origin.x + pos * pitch - extra_w * 0.5f, origin.y);
    ImVec2 pill_max(pill_min.x + btn_w + extra_w, pill_min.y + btn_h);

    // 1. Halo glow(只在 pulse > 0 时画,ease-out cubic 让头快尾慢)
    if (pulse_raw > 0.005f) {
        float p_inv = 1.f - pulse_raw;
        float ease  = 1.f - p_inv * p_inv * p_inv;
        float halo_extra = S(7.f) * ease;
        ImVec4 glow_col = col::accent;
        glow_col.w = ease * 0.45f;
        dl->AddRectFilled(
            ImVec2(pill_min.x - halo_extra, pill_min.y - halo_extra),
            ImVec2(pill_max.x + halo_extra, pill_max.y + halo_extra),
            ImGui::ColorConvertFloat4ToU32(glow_col), S(9.f));
    }

    // 2. 主 pill(被 stretch 撑开)
    dl->AddRectFilled(pill_min, pill_max, U32(col::accent), S(4.f));

    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        // 同样的道理,PushID 也要用指针(稳定)而非字符串内容。
        ImGui::PushID((const void*)slot);
        ImGui::PushID(i);

        if (i > 0) ImGui::SameLine(0, gap);
        ImVec2 cell_pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##c", ImVec2(btn_w, btn_h))) {
            size_t n = std::strlen(choices[i]);
            if (n >= slot_size) n = slot_size - 1;
            std::memcpy(slot, choices[i], n);
            slot[n] = 0;
            st->SetFloat(pulse_id, 1.f);
            // 给 vel 一个初始冲量,免得静止 → 极慢加速的迟钝感。
            float bump = (float)i - pos;
            float existing_vel = st->GetFloat(vel_id, 0.f);
            if (std::fabs(existing_vel) < std::fabs(bump) * 3.f) {
                st->SetFloat(vel_id, existing_vel + bump * 4.f);
            }
            changed = true;
        }
        bool hovered = ImGui::IsItemHovered();

        if (hovered && i != selected) {
            dl->AddRectFilled(
                cell_pos, ImVec2(cell_pos.x + btn_w, cell_pos.y + btn_h),
                U32(col::bg_hover, 0.5f), S(4.f));
        }

        float dist    = std::fabs(pos - (float)i);
        float on_pill = (dist < 1.f) ? (1.f - dist) : 0.f;
        ImU32 text_col = Mix(col::text, ImVec4(0.05f, 0.07f, 0.10f, 1.f), on_pill);

        ImVec2 text_sz = ImGui::CalcTextSize(choices[i]);
        ImVec2 text_pos(cell_pos.x + (btn_w - text_sz.x) * 0.5f,
                        cell_pos.y + (btn_h - text_sz.y) * 0.5f);
        dl->AddText(text_pos, text_col, choices[i]);

        ImGui::PopID();
        ImGui::PopID();
    }
    return changed;
}

static void DrawActivity(State& s) {
    // ----- 卡片 1:NOW -----
    SectionTitle(i18n::t("NOW", "\xE7\x8E\xB0\xE5\x9C\xA8", "\xE7\x8F\xBE\xE5\x9C\xA8"));
    CardBegin("##card_act_now");

    // 检测到的前台应用
    ImGui::PushFont(font_title);
    if (s.foreground_app[0]) {
        const char* emo = CategoryEmojiFromState(s, (util::AppCategory)s.foreground_category);
        ImGui::TextColored(col::text, "%s %s", emo, s.foreground_app);
    } else {
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("(no foreground app)",
                    "(\xE6\x97\xA0\xE5\x89\x8D\xE5\x8F\xB0\xE5\xBA\x94\xE7\x94\xA8)",
                    "(\xE7\x84\xA1\xE5\x89\x8D\xE5\x8F\xB0\xE6\x87\x89\xE7\x94\xA8)"));
    }
    ImGui::PopFont();

    // 键鼠空闲
    ImGui::PushFont(font_body);
    uint32_t sec = s.idle_seconds;
    char idle_buf[64];
    if (sec < 60) {
        std::snprintf(idle_buf, sizeof(idle_buf), "%us", sec);
    } else if (sec < 3600) {
        std::snprintf(idle_buf, sizeof(idle_buf), "%um %us", sec / 60, sec % 60);
    } else {
        std::snprintf(idle_buf, sizeof(idle_buf), "%uh %um", sec / 3600, (sec % 3600) / 60);
    }
    bool afk_active = s.afk_auto && s.afk_threshold_min > 0 &&
                      (int)sec >= s.afk_threshold_min * 60;
    ImGui::TextColored(afk_active ? col::accent : col::text_dim, "%s %s%s",
        i18n::t("Idle:", "\xE7\xA9\xBA\xE9\x97\xB2\xEF\xBC\x9A", "\xE7\xA9\xBA\xE9\x96\x92\xEF\xBC\x9A"),
        idle_buf,
        afk_active ? i18n::t("  (AFK)", "  \xEF\xBC\x88\xE5\xB7\xB2 AFK\xEF\xBC\x89", "  \xEF\xBC\x88\xE5\xB7\xB2 AFK\xEF\xBC\x89") : "");

    // 实时预览:effective prefix
    std::string prefix = EffectiveStatusPrefix(s);
    if (!prefix.empty()) {
        ImGui::Dummy(ImVec2(0, S(4.f)));
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("Chatbox prefix:",
                    "Chatbox \xE5\x89\x8D\xE7\xBC\x80\xEF\xBC\x9A",
                    "Chatbox \xE5\x89\x8D\xE7\xB6\xB4\xEF\xBC\x9A"));
        ImGui::TextColored(col::accent, "%s", prefix.c_str());
    }
    ImGui::PopFont();
    CardEnd();

    // ----- 卡片 2:STATUS OVERRIDE -----
    SectionTitle(i18n::t("STATUS OVERRIDE",
                         "\xE8\x87\xAA\xE5\xAE\x9A\xE4\xB9\x89\xE7\x8A\xB6\xE6\x80\x81",
                         "\xE8\x87\xAA\xE5\xAE\x9A\xE7\xBE\xA9\xE7\x8B\x80\xE6\x85\x8B"));
    CardBegin("##card_act_status");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Type a custom status, or pick a preset. Overrides AFK and foreground app.",
                "\xE8\xBE\x93\xE5\x85\xA5\xE8\x87\xAA\xE5\xAE\x9A\xE4\xB9\x89\xE7\x8A\xB6\xE6\x80\x81\xE6\x88\x96\xE7\x82\xB9\xE9\xA2\x84\xE8\xAE\xBE\xE3\x80\x82\xE4\xBC\x9A\xE9\xA1\xB6\xE6\x8E\x89 AFK \xE5\x92\x8C\xE5\x89\x8D\xE5\x8F\xB0\xE5\xBA\x94\xE7\x94\xA8\xE3\x80\x82",
                "\xE8\xBC\xB8\xE5\x85\xA5\xE8\x87\xAA\xE5\xAE\x9A\xE7\xBE\xA9\xE7\x8B\x80\xE6\x85\x8B\xE6\x88\x96\xE9\xBB\x9E\xE9\xA0\x90\xE8\xA8\xAD\xE3\x80\x82\xE6\x9C\x83\xE9\xA0\x82\xE6\x8E\x89 AFK \xE8\x88\x87\xE5\x89\x8D\xE5\x8F\xB0\xE6\x87\x89\xE7\x94\xA8\xE3\x80\x82"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4.f)));

    // 文本输入框
    if (NLInputText("##status_text",
            i18n::t("e.g. \"in a meeting\", \"studying\", \"AFK 20m\"",
                    "\xE4\xBE\x8B\xE5\xA6\x82 \"\xE5\xBC\x80\xE4\xBC\x9A\xE4\xB8\xAD\" / \"\xE5\xAD\xA6\xE4\xB9\xA0\xE4\xB8\xAD\" / \"AFK 20\xE5\x88\x86\"",
                    "\xE4\xBE\x8B\xE5\xA6\x82 \"\xE9\x96\x8B\xE6\x9C\x83\xE4\xB8\xAD\" / \"\xE5\xAD\xB8\xE7\xBF\x92\xE4\xB8\xAD\" / \"AFK 20\xE5\x88\x86\""),
            s.status_override, sizeof(s.status_override))) {
        // 用户手动改了:如果还没 emoji 给一个 📝;并刷新倒计时。
        if (s.status_override[0] && !s.status_override_emoji[0]) {
            strcpy_s(s.status_override_emoji, sizeof(s.status_override_emoji), "\xF0\x9F\x93\x9D");
        }
        s.status_override_remaining_sec = s.status_override_clear_min * 60;
    }

    // 4 个预设 + 清除
    ImGui::Dummy(ImVec2(0, S(4.f)));
    struct Preset { const char* emoji; const char *en, *sc, *tc; };
    static const Preset presets[] = {
        { "\xF0\x9F\x9A\xB6", "BRB",    "\xE9\xA9\xAC\xE4\xB8\x8A\xE5\x9B\x9E", "\xE9\xA6\xAC\xE4\xB8\x8A\xE5\x9B\x9E" },
        { "\xF0\x9F\x92\xA4", "Sleep",  "\xE7\x9D\xA1\xE8\xA7\x89",             "\xE7\x9D\xA1\xE8\xA6\xBA"             },
        { "\xF0\x9F\x92\xBC", "Work",   "\xE5\xB7\xA5\xE4\xBD\x9C\xE4\xB8\xAD", "\xE5\xB7\xA5\xE4\xBD\x9C\xE4\xB8\xAD" },
        { "\xF0\x9F\x8E\xAC", "Stream", "\xE7\x9B\xB4\xE6\x92\xAD\xE4\xB8\xAD", "\xE7\x9B\xB4\xE6\x92\xAD\xE4\xB8\xAD" },
    };
    float btn_w = (ImGui::GetContentRegionAvail().x - S(8.f) - S(4.f) * 4) / 5.f;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine(0, S(4.f));
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "%s %s##preset%d", presets[i].emoji,
                      i18n::current == i18n::Lang::SC ? presets[i].sc :
                      i18n::current == i18n::Lang::TC ? presets[i].tc : presets[i].en,
                      i);
        if (NLButton(lbl, btn_w, S(28.f), /*accent*/false)) {
            std::snprintf(s.status_override, sizeof(s.status_override), "%s",
                          i18n::current == i18n::Lang::SC ? presets[i].sc :
                          i18n::current == i18n::Lang::TC ? presets[i].tc : presets[i].en);
            strcpy_s(s.status_override_emoji, sizeof(s.status_override_emoji), presets[i].emoji);
            s.status_override_remaining_sec = s.status_override_clear_min * 60;
        }
    }
    ImGui::SameLine(0, S(4.f));
    if (NLButton(i18n::t("Clear##preset_clr",
                         "\xE6\xB8\x85\xE9\x99\xA4##preset_clr",
                         "\xE6\xB8\x85\xE9\x99\xA4##preset_clr"),
                 btn_w, S(28.f), /*accent*/false, /*danger*/true)) {
        s.status_override[0]       = 0;
        s.status_override_emoji[0] = 0;
        s.status_override_remaining_sec = 0;
    }

    // 自动清除倒计时下拉
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Auto-clear after",
                "\xE8\x87\xAA\xE5\x8A\xA8\xE6\xB8\x85\xE9\x99\xA4\xEF\xBC\x9A",
                "\xE8\x87\xAA\xE5\x8B\x95\xE6\xB8\x85\xE9\x99\xA4\xEF\xBC\x9A"));
    ImGui::PopFont();
    static const char* clear_en[] = { "Never", "5 min", "30 min", "1 hour" };
    static const char* clear_sc[] = { "\xE6\xB0\xB8\xE4\xB9\x85", "5 \xE5\x88\x86\xE9\x92\x9F", "30 \xE5\x88\x86\xE9\x92\x9F", "1 \xE5\xB0\x8F\xE6\x97\xB6" };
    static const char* clear_tc[] = { "\xE6\xB0\xB8\xE4\xB9\x85", "5 \xE5\x88\x86\xE9\x90\x98", "30 \xE5\x88\x86\xE9\x90\x98", "1 \xE5\xB0\x8F\xE6\x99\x82" };
    static const int   clear_vals[] = { 0, 5, 30, 60 };
    int cur_idx = 0;
    for (int i = 0; i < 4; ++i) if (clear_vals[i] == s.status_override_clear_min) { cur_idx = i; break; }
    const char** clear_labels =
        i18n::current == i18n::Lang::SC ? clear_sc :
        i18n::current == i18n::Lang::TC ? clear_tc : clear_en;
    if (NLCombo("##clear_combo", &cur_idx, clear_labels, 4)) {
        s.status_override_clear_min = clear_vals[cur_idx];
        s.status_override_remaining_sec = s.status_override_clear_min * 60;
    }

    // 显示剩余时间
    if (s.status_override[0] && s.status_override_clear_min > 0) {
        ImGui::PushFont(font_caption);
        int rem = s.status_override_remaining_sec;
        if (rem < 60) {
            ImGui::TextColored(col::text_dim, "%s %ds",
                i18n::t("Clears in", "\xE5\x89\xA9\xE4\xBD\x99", "\xE5\x89\xA9\xE9\xA4\x98"), rem);
        } else {
            ImGui::TextColored(col::text_dim, "%s %dm %ds",
                i18n::t("Clears in", "\xE5\x89\xA9\xE4\xBD\x99", "\xE5\x89\xA9\xE9\xA4\x98"), rem / 60, rem % 60);
        }
        ImGui::PopFont();
    }
    CardEnd();

    // ----- 卡片 3:AFK AUTO -----
    SectionTitle(i18n::t("AFK AUTO-DETECT",
                         "AFK \xE8\x87\xAA\xE5\x8A\xA8\xE6\xA3\x80\xE6\xB5\x8B",
                         "AFK \xE8\x87\xAA\xE5\x8B\x95\xE5\x81\xB5\xE6\xB8\xAC"));
    CardBegin("##card_act_afk");
    NLToggle(i18n::t("Set status to \"AFK\" after idle threshold",
                     "\xE9\x94\xAE\xE9\xBC\xA0\xE7\xA9\xBA\xE9\x97\xB2\xE8\xB6\x85\xE9\x98\x88\xE5\x80\xBC\xE5\x90\x8E\xE8\x87\xAA\xE5\x8A\xA8\xE6\x98\xBE\xE7\xA4\xBA AFK",
                     "\xE9\x8D\xB5\xE9\xBC\xA0\xE7\xA9\xBA\xE9\x96\x92\xE8\xB6\x85\xE9\x96\xBE\xE5\x80\xBC\xE5\xBE\x8C\xE8\x87\xAA\xE5\x8B\x95\xE9\xA1\xAF\xE7\xA4\xBA AFK"),
             &s.afk_auto);
    NLSliderInt(i18n::t("Threshold (min)",
                        "\xE9\x98\x88\xE5\x80\xBC\xEF\xBC\x88\xE5\x88\x86\xE9\x92\x9F\xEF\xBC\x89",
                        "\xE9\x96\xBE\xE5\x80\xBC\xEF\xBC\x88\xE5\x88\x86\xE9\x90\x98\xEF\xBC\x89"),
                &s.afk_threshold_min, 1, 60);
    CardEnd();

    // ----- 卡片 4:BROADCAST -----
    SectionTitle(i18n::t("BROADCAST", "\xE5\xB9\xBF\xE6\x92\xAD", "\xE5\xBB\xA3\xE6\x92\xAD"));
    CardBegin("##card_act_send");
    NLToggle(i18n::t("Append foreground app to chatbox",
                     "\xE5\x89\x8D\xE5\x8F\xB0\xE5\xBA\x94\xE7\x94\xA8\xE9\x99\x84\xE5\x8A\xA0\xE5\x88\xB0 chatbox",
                     "\xE5\x89\x8D\xE5\x8F\xB0\xE6\x87\x89\xE7\x94\xA8\xE9\x99\x84\xE5\x8A\xA0\xE5\x88\xB0 chatbox"),
             &s.show_foreground_app);
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Only takes effect when no custom status / AFK is active.",
                "\xE4\xBB\x85\xE5\x9C\xA8\xE6\x97\xA0\xE8\x87\xAA\xE5\xAE\x9A\xE4\xB9\x89\xE7\x8A\xB6\xE6\x80\x81\xE4\xB8\x94\xE6\x9C\xAA AFK \xE6\x97\xB6\xE7\x94\x9F\xE6\x95\x88\xE3\x80\x82",
                "\xE5\x83\x85\xE5\x9C\xA8\xE7\x84\xA1\xE8\x87\xAA\xE5\xAE\x9A\xE7\xBE\xA9\xE7\x8B\x80\xE6\x85\x8B\xE4\xB8\x94\xE6\x9C\xAA AFK \xE6\x99\x82\xE7\x94\x9F\xE6\x95\x88\xE3\x80\x82"));
    ImGui::PopFont();
    CardEnd();

    // ----- 卡片 5:分类图标自定义 -----
    SectionTitle(i18n::t("CATEGORY EMOJI",
                         "\xE5\x88\x86\xE7\xB1\xBB\xE5\x9B\xBE\xE6\xA0\x87",
                         "\xE5\x88\x86\xE9\xA1\x9E\xE5\x9C\x96\xE7\xA4\xBA"));
    CardBegin("##card_act_emoji");
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Pick the emoji shown before the foreground app name.",
                "\xE9\x80\x89\xE6\x8B\xA9\xE5\x90\x84\xE5\x88\x86\xE7\xB1\xBB\xE5\xBA\x94\xE7\x94\xA8\xE5\x90\x8D\xE5\x89\x8D\xE7\x9A\x84 emoji\xE3\x80\x82",
                "\xE9\x81\xB8\xE6\x93\x87\xE5\x90\x84\xE5\x88\x86\xE9\xA1\x9E\xE6\x87\x89\xE7\x94\xA8\xE5\x90\x8D\xE5\x89\x8D\xE7\x9A\x84 emoji\xE3\x80\x82"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4.f)));

    // 每个分类一行:label + 4 个 emoji 按钮
    struct EmojiRow {
        const char* label_en; const char* label_sc; const char* label_tc;
        char*       slot;          // 指向 state 里的 emoji_* 字段
        const char* choices[4];    // 4 个候选
    };
    EmojiRow rows[] = {
        { "Games",   "\xE6\xB8\xB8\xE6\x88\x8F",   "\xE9\x81\x8A\xE6\x88\xB2",   s.emoji_game,
            { "\xF0\x9F\x8E\xAE", "\xF0\x9F\x95\xB9", "\xF0\x9F\x91\xBE", "\xF0\x9F\x8E\xAF" }},   // 🎮 🕹 👾 🎯
        { "Browser", "\xE6\xB5\x8F\xE8\xA7\x88\xE5\x99\xA8", "\xE7\x80\x8F\xE8\xA6\xBD\xE5\x99\xA8", s.emoji_browser,
            { "\xF0\x9F\x8C\x90", "\xF0\x9F\xA7\xAD", "\xF0\x9F\x94\x97", "\xF0\x9F\x93\x96" }},   // 🌐 🧭 🔗 📖
        { "Chat",    "\xE8\x81\x8A\xE5\xA4\xA9",   "\xE8\x81\x8A\xE5\xA4\xA9",   s.emoji_chat,
            { "\xF0\x9F\x92\xAC", "\xF0\x9F\x92\xAD", "\xF0\x9F\x97\xA8", "\xF0\x9F\x93\x9E" }},   // 💬 💭 🗨 📞
        { "IDE",     "\xE5\xBC\x80\xE5\x8F\x91",   "\xE9\x96\x8B\xE7\x99\xBC",   s.emoji_dev,
            { "\xF0\x9F\x92\xBB", "\xE2\x8C\xA8", "\xF0\x9F\x9B\xA0", "\xF0\x9F\x90\x9B" }},       // 💻 ⌨ 🛠 🐛
        { "Music",   "\xE9\x9F\xB3\xE4\xB9\x90",   "\xE9\x9F\xB3\xE6\xA8\x82",   s.emoji_music,
            { "\xF0\x9F\x8E\xB5", "\xF0\x9F\x8E\xB6", "\xF0\x9F\x8E\xA7", "\xF0\x9F\x8E\xA4" }},   // 🎵 🎶 🎧 🎤
        { "Office",  "\xE5\x8A\x9E\xE5\x85\xAC",   "\xE8\xBE\xA6\xE5\x85\xAC",   s.emoji_office,
            { "\xF0\x9F\x93\x84", "\xF0\x9F\x93\x8A", "\xF0\x9F\x93\x9D", "\xF0\x9F\x93\x9A" }},   // 📄 📊 📝 📚
        { "Stream",  "\xE5\x88\x9B\xE4\xBD\x9C",   "\xE5\x89\xB5\xE4\xBD\x9C",   s.emoji_stream,
            { "\xF0\x9F\x8E\xAC", "\xF0\x9F\x93\xB9", "\xF0\x9F\x94\xB4", "\xF0\x9F\x93\xBD" }},   // 🎬 📹 🔴 📽
    };

    for (auto& row : rows) {
        ImGui::PushFont(font_body);
        ImGui::TextColored(col::text_dim, "%-8s",
            i18n::current == i18n::Lang::SC ? row.label_sc :
            i18n::current == i18n::Lang::TC ? row.label_tc : row.label_en);
        ImGui::PopFont();
        ImGui::SameLine(S(80.f));
        // 滑动 pill + 点击 halo,跨帧弹簧由 ImGuiStorage 持久化。
        // ID 用 slot 内存地址当 cookie(见 AnimatedEmojiPicker 内的说明)。
        AnimatedEmojiPicker(row.slot, sizeof(s.emoji_game), row.choices);
    }
    CardEnd();
}

static void DrawAudio(State& s) {
    auto cstr_copy = [](char* dst, size_t cap, const char* src) {
        if (!cap) return;
        size_t i = 0;
        while (i + 1 < cap && src[i]) { dst[i] = src[i]; ++i; }
        dst[i] = 0;
    };
    SectionTitle(i18n::t("STATUS", "状态", "狀態"));
    CardBegin("##card_audio_status");
    ImGui::PushFont(font_body);
    ImVec4 ok_col(0.40f, 0.86f, 0.50f, 1.f);
    ImVec4 bad_col(0.85f, 0.55f, 0.30f, 1.f);

    StatusRow(s.audio_netease_detected,
              s.audio_netease_detected
                  ? i18n::t("Netease detected",
                            "\xE7\xBD\x91\xE6\x98\x93\xE4\xBA\x91\xE5\xB7\xB2\xE6\xA3\x80\xE6\xB5\x8B",
                            "\xE7\xB6\xB2\xE6\x98\x93\xE9\x9B\xB2\xE5\xB7\xB2\xE5\x81\xB5\xE6\xB8\xAC")
                  : i18n::t("Netease not running",
                            "\xE7\xBD\x91\xE6\x98\x93\xE4\xBA\x91\xE6\x9C\xAA\xE8\xBF\x90\xE8\xA1\x8C",
                            "\xE7\xB6\xB2\xE6\x98\x93\xE9\x9B\xB2\xE6\x9C\xAA\xE9\x81\x8B\xE8\xA1\x8C"),
              ok_col, bad_col);
    StatusRow(s.audio_vbcable_installed,
              s.audio_vbcable_installed
                  ? i18n::t("VB-Cable installed",
                            "VB-Cable \xE5\xB7\xB2\xE5\xAE\x89\xE8\xA3\x85",
                            "VB-Cable \xE5\xB7\xB2\xE5\xAE\x89\xE8\xA3\x9D")
                  : i18n::t("VB-Cable not installed",
                            "VB-Cable \xE6\x9C\xAA\xE5\xAE\x89\xE8\xA3\x85",
                            "VB-Cable \xE6\x9C\xAA\xE5\xAE\x89\xE8\xA3\x9D"),
              ok_col, bad_col);
    if (s.audio_relay_running) {
        StatusRow(true,
                  i18n::t("Relay running", "中继运行中", "中繼運行中"),
                  ok_col, bad_col);
    }
    if (s.audio_status_text[0]) {
        ImGui::TextColored(col::text_dim, "%s", s.audio_status_text);
    }
    ImGui::PopFont();
    CardEnd();

    // Install card — only shown when not installed.
    if (!s.audio_vbcable_installed) {
        SectionTitle(i18n::t("INSTALL VB-CABLE", "\xE5\xAE\x89\xE8\xA3\x85 VB-CABLE", "\xE5\xAE\x89\xE8\xA3\x9D VB-CABLE"));
        CardBegin("##card_audio_install");
        ImGui::PushFont(font_body);
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("VB-Cable is a free virtual audio cable driver. Click below to download and install automatically.",
                    "VB-Cable \xE6\x98\xAF\xE5\x85\x8D\xE8\xB4\xB9\xE7\x9A\x84\xE8\x99\x9A\xE6\x8B\x9F\xE5\xA3\xB0\xE5\x8D\xA1\xE9\xA9\xB1\xE5\x8A\xA8\xE3\x80\x82\xE7\x82\xB9\xE5\x87\xBB\xE4\xB8\x8B\xE6\x96\xB9\xE8\x87\xAA\xE5\x8A\xA8\xE4\xB8\x8B\xE8\xBD\xBD\xE5\xAE\x89\xE8\xA3\x85\xE3\x80\x82",
                    "VB-Cable \xE6\x98\xAF\xE5\x85\x8D\xE8\xB2\xBB\xE7\x9A\x84\xE8\x99\x9B\xE6\x93\xAC\xE8\x81\xB2\xE5\x8D\xA1\xE9\xA9\x85\xE5\x8B\x95\xE3\x80\x82\xE9\xBB\x9E\xE6\x93\x8A\xE4\xB8\x8B\xE6\x96\xB9\xE8\x87\xAA\xE5\x8B\x95\xE4\xB8\x8B\xE8\xBC\x89\xE5\xAE\x89\xE8\xA3\x9D\xE3\x80\x82"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(6.f)));

        bool busy = (s.audio_install_step >= 0 && s.audio_install_step <= 4);
        if (!busy) {
            float w = ImGui::GetContentRegionAvail().x - S(14.f);
            ImGui::PushFont(font_medium);
            const char* lbl = (s.audio_install_step == (int)6 /*Failed*/)
                ? i18n::t("Retry install", "\xE9\x87\x8D\xE8\xAF\x95\xE5\xAE\x89\xE8\xA3\x85", "\xE9\x87\x8D\xE8\xA9\xA6\xE5\xAE\x89\xE8\xA3\x9D")
                : i18n::t("Download and install", "\xE4\xB8\x8B\xE8\xBD\xBD\xE5\xB9\xB6\xE5\xAE\x89\xE8\xA3\x85", "\xE4\xB8\x8B\xE8\xBC\x89\xE4\xB8\xA6\xE5\xAE\x89\xE8\xA3\x9D");
            if (NLButton(lbl, w, S(34.f), /*accent*/true)) {
                s.audio_install_request = true;
            }
            ImGui::PopFont();
        }

        if (s.audio_install_step >= 0) {
            ImGui::Dummy(ImVec2(0, S(6.f)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x - S(14.f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(6.f)), U32(col::bg_input), S(3.f));
            float frac = 0.f;
            switch (s.audio_install_step) {
                case 0: frac = 0.10f + 0.30f * s.audio_install_fraction; break; // Downloading
                case 1: frac = 0.50f; break; // Extracting
                case 2: frac = 0.60f; break; // LaunchingInstaller
                case 3: frac = 0.70f; break; // AwaitingUser
                case 4: frac = 0.90f; break; // Verifying
                case 5: frac = 1.00f; break; // Done
                case 6: frac = s.audio_install_fraction; break; // Failed
                default: break;
            }
            // 安装步骤是离散跳变(0.1→0.5→0.6→0.7→0.9→1.0),走低通滤波
            // 看起来才有连贯性,不至于一段段卡帧。
            float anim_frac = AnimatedFraction(ImGui::GetID("##install_frac"), frac);
            if (anim_frac > 0.f) {
                dl->AddRectFilled(p, ImVec2(p.x + w * anim_frac, p.y + S(6.f)),
                                  U32(s.audio_install_step == 6 ? ImVec4(0.85f, 0.45f, 0.40f, 1.f) : col::accent),
                                  S(3.f));
            }
            ImGui::Dummy(ImVec2(w, S(8.f)));
            ImGui::PushFont(font_caption);
            ImGui::TextColored(col::text_dim, "%s",
                s.audio_install_msg[0] ? s.audio_install_msg : "");
            ImGui::PopFont();
        }
        CardEnd();
    }

    // Device selector
    SectionTitle(i18n::t("OUTPUT DEVICE", "\xE8\xBE\x93\xE5\x87\xBA\xE8\xAE\xBE\xE5\xA4\x87", "\xE8\xBC\xB8\xE5\x87\xBA\xE8\xA3\x9D\xE7\xBD\xAE"));
    CardBegin("##card_audio_device");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Audio is routed to this device.",
                "\xE9\x9F\xB3\xE9\xA2\x91\xE5\xB0\x86\xE5\x8F\x91\xE9\x80\x81\xE5\x88\xB0\xE6\xAD\xA4\xE8\xAE\xBE\xE5\xA4\x87\xE3\x80\x82",
                "\xE9\x9F\xB3\xE9\xA0\xBB\xE5\xB0\x87\xE5\x82\xB3\xE9\x80\x81\xE5\x88\xB0\xE6\xAD\xA4\xE8\xA3\x9D\xE7\xBD\xAE\xE3\x80\x82"));
    ImGui::PopFont();

    // Build labels for combo
    static const char* item_ptrs[16] = {};
    static char        labels[16][140];
    int  current_idx = -1;
    int  vb_idx      = -1;
    for (int i = 0; i < s.audio_device_count && i < 16; ++i) {
        const char* tag = s.audio_devices[i].is_vbcable
            ? i18n::t(" (recommended)", " (\xE6\x8E\xA8\xE8\x8D\x90)", " (\xE6\x8E\xA8\xE8\x96\xA6)")
            : (s.audio_devices[i].is_default ? i18n::t(" (default)", " (\xE9\xBB\x98\xE8\xAE\xA4)", " (\xE9\xA0\x90\xE8\xA8\xAD)") : "");
        _snprintf_s(labels[i], sizeof(labels[i]), _TRUNCATE,
                    "%s%s", s.audio_devices[i].label, tag);
        item_ptrs[i] = labels[i];
        if (std::strcmp(s.audio_devices[i].id, s.audio_target_device_id) == 0) current_idx = i;
        if (s.audio_devices[i].is_vbcable && vb_idx < 0) vb_idx = i;
    }
    if (current_idx < 0 && vb_idx >= 0) {
        current_idx = vb_idx;
        cstr_copy(s.audio_target_device_id,    sizeof(s.audio_target_device_id),    s.audio_devices[vb_idx].id);
        cstr_copy(s.audio_target_device_label, sizeof(s.audio_target_device_label), s.audio_devices[vb_idx].label);
    }
    ImGui::SetNextItemWidth(-S(80.f));
    if (s.audio_device_count > 0) {
        if (NLCombo("##audio_dev", &current_idx, item_ptrs, s.audio_device_count, ImGui::GetContentRegionAvail().x - S(80.f))) {
            if (current_idx >= 0 && current_idx < s.audio_device_count) {
                cstr_copy(s.audio_target_device_id,    sizeof(s.audio_target_device_id),    s.audio_devices[current_idx].id);
                cstr_copy(s.audio_target_device_label, sizeof(s.audio_target_device_label), s.audio_devices[current_idx].label);
            }
        }
    } else {
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("(no devices)", "(\xE6\x97\xA0\xE8\xAE\xBE\xE5\xA4\x87)", "(\xE7\x84\xA1\xE8\xA3\x9D\xE7\xBD\xAE)"));
    }
    ImGui::SameLine();
    if (NLButton(i18n::t("Refresh", "\xE5\x88\xB7\xE6\x96\xB0", "\xE5\x88\xB7\xE6\x96\xB0"),
                 S(70.f), S(34.f), /*accent*/false)) {
        s.audio_refresh_request = true;
    }
    CardEnd();

    // Relay controls
    SectionTitle(i18n::t("RELAY", "\xE4\xB8\xAD\xE7\xBB\xA7", "\xE4\xB8\xAD\xE7\xB9\xBC"));
    CardBegin("##card_audio_relay");
    const char* btn_label = s.audio_relay_running
        ? i18n::t("Stop relay", "\xE5\x81\x9C\xE6\xAD\xA2\xE4\xB8\xAD\xE7\xBB\xA7", "\xE5\x81\x9C\xE6\xAD\xA2\xE4\xB8\xAD\xE7\xB9\xBC")
        : i18n::t("Start relay", "\xE5\x90\xAF\xE5\x8A\xA8\xE4\xB8\xAD\xE7\xBB\xA7", "\xE5\x95\x9F\xE5\x8B\x95\xE4\xB8\xAD\xE7\xB9\xBC");
    float btn_w = ImGui::GetContentRegionAvail().x - S(14.f);
    float btn_h = S(36.f);
    bool can_start = s.audio_vbcable_installed && s.audio_netease_detected && s.audio_target_device_id[0];
    ImGui::PushFont(font_medium);
    if (NLButton(btn_label, btn_w, btn_h,
                 /*accent*/!s.audio_relay_running && can_start,
                 /*danger*/s.audio_relay_running,
                 /*disabled*/!s.audio_relay_running && !can_start)) {
        if (s.audio_relay_running) s.audio_stop_request = true;
        else if (can_start)        s.audio_start_request = true;
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, S(4.f)));
    {
        int v = (int)std::round(s.audio_gain_db * 10.f);
        if (NLSliderInt(i18n::t("Gain (dB x10)", "\xE5\xA2\x9E\xE7\x9B\x8A (dB x10)", "\xE5\xA2\x9E\xE7\x9B\x8A (dB x10)"), &v, -120, 120)) {
            s.audio_gain_db = (float)v / 10.f;
        }
    }
    NLToggle(i18n::t("Limiter (prevent clipping)", "\xE9\x99\x90\xE5\xB9\x85\xE5\x99\xA8\xEF\xBC\x88\xE9\x98\xB2\xE7\x88\x86\xE9\x9F\xB3\xEF\xBC\x89", "\xE9\x99\x90\xE5\xB9\x85\xE5\x99\xA8\xEF\xBC\x88\xE9\x98\xB2\xE7\x88\x86\xE9\x9F\xB3\xEF\xBC\x89"),
             &s.audio_limiter);
    NLToggle(i18n::t("Auto-start when Netease plays", "\xE7\xBD\x91\xE6\x98\x93\xE4\xBA\x91\xE6\x92\xAD\xE6\x94\xBE\xE6\x97\xB6\xE8\x87\xAA\xE5\x8A\xA8\xE5\x90\xAF\xE5\x8A\xA8", "\xE7\xB6\xB2\xE6\x98\x93\xE9\x9B\xB2\xE6\x92\xAD\xE6\x94\xBE\xE6\x99\x82\xE8\x87\xAA\xE5\x8B\x95\xE5\x95\x9F\xE5\x8B\x95"),
             &s.audio_autostart);

    // Peak meter
    ImGui::Dummy(ImVec2(0, S(6.f)));
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s %.1f dBFS",
        i18n::t("Peak", "\xE5\xB3\xB0\xE5\x80\xBC", "\xE5\xB3\xB0\xE5\x80\xBC"),
        s.audio_peak_dbfs);
    ImGui::PopFont();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x - S(14.f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(4.f)), U32(col::bg_input), S(2.f));
        // Map -60..0 dB to 0..1
        float peak = s.audio_peak_dbfs;
        float frac = (peak + 60.f) / 60.f;
        if (frac < 0.f) frac = 0.f; if (frac > 1.f) frac = 1.f;
        ImVec4 meter_col = col::accent;
        if (peak > -6.f) meter_col = ImVec4(0.85f, 0.55f, 0.30f, 1.f);
        if (peak > -1.f) meter_col = ImVec4(0.85f, 0.30f, 0.35f, 1.f);
        if (frac > 0.f) {
            dl->AddRectFilled(p, ImVec2(p.x + w * frac, p.y + S(4.f)), U32(meter_col), S(2.f));
        }
        ImGui::Dummy(ImVec2(w, S(6.f)));
    }
    CardEnd();

    // VRChat 那边的语音处理才是"闷"的主因 —— 这里直接告诉用户怎么改,免得反复来问。
    SectionTitle(i18n::t("AUDIO QUALITY TIPS",
                         "\xE9\x9F\xB3\xE8\xB4\xA8\xE5\xBB\xBA\xE8\xAE\xAE",
                         "\xE9\x9F\xB3\xE8\xB3\xAA\xE5\xBB\xBA\xE8\xAD\xB0"));
    CardBegin("##card_audio_tips");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("If listeners say the music sounds muffled or low-fi, it's usually VRChat's voice processing — not the relay. Try these in order:",
                "\xE5\xA6\x82\xE6\x9E\x9C\xE5\x88\xAB\xE4\xBA\xBA\xE5\x90\xAC\xE5\x88\xB0\xE7\x9A\x84\xE9\x9F\xB3\xE4\xB9\x90\xE5\x8F\x91\xE9\x97\xB7\xE3\x80\x81\xE5\x83\x8F\xE9\x9A\x94\xE4\xBA\x86\xE4\xB8\x80\xE5\xB1\x82\xEF\xBC\x8C\xE5\xA4\xA7\xE6\xA6\x82\xE7\x8E\x87\xE6\x98\xAF VRChat \xE7\x9A\x84\xE8\xAF\xAD\xE9\x9F\xB3\xE5\xA4\x84\xE7\x90\x86\xEF\xBC\x8C\xE4\xB8\x8D\xE6\x98\xAF\xE4\xB8\xAD\xE7\xBB\xA7\xE3\x80\x82\xE4\xBE\x9D\xE6\xAC\xA1\xE8\xAF\x95\xE8\xAF\x95\xEF\xBC\x9A",
                "\xE5\xA6\x82\xE6\x9E\x9C\xE5\x88\xA5\xE4\xBA\xBA\xE8\x81\xBD\xE5\x88\xB0\xE7\x9A\x84\xE9\x9F\xB3\xE6\xA8\x82\xE7\x99\xBC\xE6\x82\xB6\xE3\x80\x81\xE5\x83\x8F\xE9\x9A\x94\xE4\xBA\x86\xE4\xB8\x80\xE5\xB1\xA4\xEF\xBC\x8C\xE5\xA4\xA7\xE6\xA9\x9F\xE7\x8E\x87\xE6\x98\xAF VRChat \xE7\x9A\x84\xE8\xAA\x9E\xE9\x9F\xB3\xE8\x99\x95\xE7\x90\x86\xEF\xBC\x8C\xE4\xB8\x8D\xE6\x98\xAF\xE4\xB8\xAD\xE7\xB9\xBC\xE3\x80\x82\xE4\xBE\x9D\xE5\xBA\x8F\xE8\xA9\xA6\xE8\xA9\xA6\xEF\xBC\x9A"));
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::TextColored(col::text, "%s",
        i18n::t("1. VRChat: Settings -> Audio & Voice -> Voice Processing -> None",
                "1. VRChat \xEF\xBC\x9A Settings -> Audio & Voice -> Voice Processing \xE6\x94\xB9\xE6\x88\x90 None\xEF\xBC\x88\xE5\x85\xB3\xE9\x97\xAD\xE5\x99\xAA\xE5\xA3\xB0\xE6\x8A\x91\xE5\x88\xB6\xEF\xBC\x89",
                "1. VRChat \xEF\xBC\x9A Settings -> Audio & Voice -> Voice Processing \xE6\x94\xB9\xE6\x88\x90 None\xEF\xBC\x88\xE9\x97\x9C\xE9\x96\x89\xE5\x99\xAA\xE8\xA8\x8A\xE6\x8A\x91\xE5\x88\xB6\xEF\xBC\x89"));
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("   The single biggest fix. VRChat applies noise suppression on the mic input by default, which destroys music high frequencies.",
                "   \xE5\x8D\x95\xE7\x82\xB9\xE6\x94\xB6\xE7\x9B\x8A\xE6\x9C\x80\xE5\xA4\xA7\xE7\x9A\x84\xE4\xB8\x80\xE9\xA1\xB9\xE3\x80\x82VRChat \xE9\xBB\x98\xE8\xAE\xA4\xE5\xAF\xB9\xE9\xBA\xA6\xE5\x85\x8B\xE9\xA3\x8E\xE5\x81\x9A\xE5\x99\xAA\xE5\xA3\xB0\xE6\x8A\x91\xE5\x88\xB6\xEF\xBC\x8C\xE5\xAF\xB9\xE9\x9F\xB3\xE4\xB9\x90\xE7\x9A\x84\xE9\xAB\x98\xE9\xA2\x91\xE5\x87\xA0\xE4\xB9\x8E\xE7\x81\xAD\xE6\x80\xA7\xE6\x89\x93\xE5\x87\xBB\xE3\x80\x82",
                "   \xE5\x96\xAE\xE9\xBB\x9E\xE6\x94\xB6\xE7\x9B\x8A\xE6\x9C\x80\xE5\xA4\xA7\xE7\x9A\x84\xE4\xB8\x80\xE9\xA0\x85\xE3\x80\x82VRChat \xE9\xA0\x90\xE8\xA8\xAD\xE5\xB0\x8D\xE9\xBA\xA5\xE5\x85\x8B\xE9\xA2\xA8\xE5\x81\x9A\xE5\x99\xAA\xE8\xA8\x8A\xE6\x8A\x91\xE5\x88\xB6\xEF\xBC\x8C\xE5\xB0\x8D\xE9\x9F\xB3\xE6\xA8\x82\xE7\x9A\x84\xE9\xAB\x98\xE9\xA0\xBB\xE5\xB9\xBE\xE4\xB9\x8E\xE7\x81\xAB\xE6\x80\xA7\xE6\x89\x93\xE6\x93\x8A\xE3\x80\x82"));
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::TextColored(col::text, "%s",
        i18n::t("2. Windows: Sound -> CABLE Input -> Properties -> Advanced -> 24bit 48000Hz",
                "2. Windows \xE5\xA3\xB0\xE9\x9F\xB3\xE8\xAE\xBE\xE7\xBD\xAE\xE2\x86\x92 CABLE Input \xE2\x86\x92 \xE5\xB1\x9E\xE6\x80\xA7\xE2\x86\x92\xE9\xAB\x98\xE7\xBA\xA7\xE2\x86\x92 24 \xE4\xBD\x8D 48000Hz",
                "2. Windows \xE8\x81\xB2\xE9\x9F\xB3\xE8\xA8\xAD\xE5\xAE\x9A\xE2\x86\x92 CABLE Input \xE2\x86\x92 \xE5\xB1\xAC\xE6\x80\xA7\xE2\x86\x92\xE9\x80\xB2\xE9\x9A\x8E\xE2\x86\x92 24 \xE4\xBD\x8D 48000Hz"));
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("   VB-Cable defaults to 16bit 44100Hz on some systems. Sets a low-quality SRC stage in the audio engine.",
                "   \xE9\x83\xA8\xE5\x88\x86\xE7\xB3\xBB\xE7\xBB\x9F VB-Cable \xE9\xBB\x98\xE8\xAE\xA4 16 \xE4\xBD\x8D 44100Hz\xEF\xBC\x8C\xE4\xBC\x9A\xE5\x9C\xA8\xE9\x9F\xB3\xE9\xA2\x91\xE5\xBC\x95\xE6\x93\x8E\xE9\x87\x8C\xE5\xA4\x9A\xE5\x8A\xA0\xE4\xB8\x80\xE9\x81\x93\xE4\xBD\x8E\xE8\xB4\xA8 SRC\xE3\x80\x82\xE6\x94\xB9\xE5\xAE\x8C\xE9\x87\x8D\xE5\x90\xAF\xE4\xB8\xAD\xE7\xBB\xA7\xE7\x94\x9F\xE6\x95\x88\xE3\x80\x82",
                "   \xE9\x83\xA8\xE5\x88\x86\xE7\xB3\xBB\xE7\xB5\xB1 VB-Cable \xE9\xA0\x90\xE8\xA8\xAD 16 \xE4\xBD\x8D 44100Hz\xEF\xBC\x8C\xE6\x9C\x83\xE5\x9C\xA8\xE9\x9F\xB3\xE9\xA0\xBB\xE5\xBC\x95\xE6\x93\x8E\xE8\xA3\xA1\xE5\xA4\x9A\xE5\x8A\xA0\xE4\xB8\x80\xE9\x81\x93\xE4\xBD\x8E\xE8\xB3\xAA SRC\xE3\x80\x82\xE6\x94\xB9\xE5\xAE\x8C\xE9\x87\x8D\xE5\x95\x9F\xE4\xB8\xAD\xE7\xB9\xBC\xE7\x94\x9F\xE6\x95\x88\xE3\x80\x82"));
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::TextColored(col::text, "%s",
        i18n::t("3. VRChat: turn the mic volume slider up to 100% or higher.",
                "3. VRChat \xE9\xBA\xA6\xE5\x85\x8B\xE9\xA3\x8E\xE9\x9F\xB3\xE9\x87\x8F\xE6\x8B\x89\xE5\x88\xB0 100%% \xE6\x88\x96\xE6\x9B\xB4\xE9\xAB\x98\xE3\x80\x82",
                "3. VRChat \xE9\xBA\xA5\xE5\x85\x8B\xE9\xA2\xA8\xE9\x9F\xB3\xE9\x87\x8F\xE6\x8B\x89\xE5\x88\xB0 100%% \xE6\x88\x96\xE6\x9B\xB4\xE9\xAB\x98\xE3\x80\x82"));
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("   VRChat doesn't auto-normalize the mic; quieter signal = lower SNR through Opus.",
                "   VRChat \xE4\xB8\x8D\xE4\xBC\x9A\xE8\x87\xAA\xE5\x8A\xA8\xE5\xBD\x92\xE4\xB8\x80\xE5\x8C\x96\xE9\xBA\xA6\xE5\x85\x8B\xE9\xA3\x8E\xEF\xBC\x8C\xE4\xBF\xA1\xE5\x8F\xB7\xE8\xB6\x8A\xE5\xB0\x8F\xE7\xBB\x8F\xE8\xBF\x87 Opus \xE7\xBC\x96\xE7\xA0\x81\xE5\x90\x8E\xE4\xBF\xA1\xE5\x99\xAA\xE6\xAF\x94\xE8\xB6\x8A\xE4\xBD\x8E\xE3\x80\x82",
                "   VRChat \xE4\xB8\x8D\xE6\x9C\x83\xE8\x87\xAA\xE5\x8B\x95\xE6\xAD\xB8\xE4\xB8\x80\xE5\x8C\x96\xE9\xBA\xA5\xE5\x85\x8B\xE9\xA2\xA8\xEF\xBC\x8C\xE4\xBF\xA1\xE8\x99\x9F\xE8\xB6\x8A\xE5\xB0\x8F\xE7\xB6\x93\xE9\x81\x8E Opus \xE7\xB7\xA8\xE7\xA2\xBC\xE5\xBE\x8C\xE4\xBF\xA1\xE5\x99\xAA\xE6\xAF\x94\xE8\xB6\x8A\xE4\xBD\x8E\xE3\x80\x82"));
    ImGui::PopFont();
    CardEnd();
}

static void DrawVideoTab(State& s) {
    if (s.video_copy_toast_sec > 0.f) {
        s.video_copy_toast_sec -= ImGui::GetIO().DeltaTime;
        if (s.video_copy_toast_sec < 0.f) s.video_copy_toast_sec = 0.f;
    }
    SectionTitle(i18n::t("BILIBILI PARSER",
                         "\xE5\x93\x94\xE5\x93\xA9\xE5\x93\x94\xE5\x93\xA9\xE8\xA7\xA3\xE6\x9E\x90",
                         "\xE5\x93\x94\xE5\x93\xA9\xE5\x93\x94\xE5\x93\xA9\xE8\xA7\xA3\xE6\x9E\x90"));
    CardBegin("##card_video_input");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Paste a BV id, full bilibili URL, or a b23.tv short link.",
                "\xE7\xB2\x98\xE8\xB4\xB4 BV \xE5\x8F\xB7\xE3\x80\x81\xE5\xAE\x8C\xE6\x95\xB4 bilibili \xE9\x93\xBE\xE6\x8E\xA5\xE6\x88\x96 b23.tv \xE7\x9F\xAD\xE9\x93\xBE\xE3\x80\x82",
                "\xE8\xB2\xBC\xE4\xB8\x8A BV \xE8\x99\x9F\xE3\x80\x81\xE5\xAE\x8C\xE6\x95\xB4 bilibili \xE9\x80\xA3\xE7\xB5\x90\xE6\x88\x96 b23.tv \xE7\x9F\xAD\xE9\x80\xA3\xE3\x80\x82"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(6.f)));

    NLInputText("##video_in",
        "BV1xx411c7mu  /  https://www.bilibili.com/video/...  /  https://b23.tv/...",
        s.video_input, sizeof(s.video_input),
        ImGui::GetContentRegionAvail().x - S(14.f));  // 右边内缩 14px,跟卡片里其它内容(如进度条)对齐

    ImGui::Dummy(ImVec2(0, S(4.f)));
    bool busy = (s.video_status == 1);
    float btn_w = ImGui::GetContentRegionAvail().x - S(14.f);
    float btn_h = S(36.f);
    ImGui::PushFont(font_medium);
    const char* btn_label = busy
        ? i18n::t("Parsing...",
                  "\xE8\xA7\xA3\xE6\x9E\x90\xE4\xB8\xAD...",
                  "\xE8\xA7\xA3\xE6\x9E\x90\xE4\xB8\xAD...")
        : i18n::t("Parse",
                  "\xE8\xA7\xA3\xE6\x9E\x90",
                  "\xE8\xA7\xA3\xE6\x9E\x90");
    if (NLButton(btn_label, btn_w, btn_h, /*accent*/!busy, /*danger*/false, /*disabled*/busy) &&
        !busy && s.video_input[0]) {
        s.video_parse_request = true;
    }
    ImGui::PopFont();
    CardEnd();

    SectionTitle(i18n::t("RESULT",
                         "\xE7\xBB\x93\xE6\x9E\x9C",
                         "\xE7\xB5\x90\xE6\x9E\x9C"));
    CardBegin("##card_video_result");
    ImGui::PushFont(font_body);
    ImVec4 ok_col(0.40f, 0.86f, 0.50f, 1.f);
    ImVec4 bad_col(0.85f, 0.55f, 0.30f, 1.f);

    if (s.video_status == 1) {
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("Resolving short link and fetching playurl...",
                    "\xE6\xAD\xA3\xE5\x9C\xA8\xE8\xA7\xA3\xE6\x9E\x90\xE7\x9F\xAD\xE9\x93\xBE\xE5\xB9\xB6\xE8\xAF\xB7\xE6\xB1\x82 playurl...",
                    "\xE6\xAD\xA3\xE5\x9C\xA8\xE8\xA7\xA3\xE6\x9E\x90\xE7\x9F\xAD\xE9\x80\xA3\xE4\xB8\xA6\xE8\xAB\x8B\xE6\xB1\x82 playurl..."));
    } else if (s.video_status == 3) {
        StatusRow(false,
                  s.video_error[0] ? s.video_error
                                   : i18n::t("Parse failed.",
                                             "\xE8\xA7\xA3\xE6\x9E\x90\xE5\xA4\xB1\xE8\xB4\xA5\xE3\x80\x82",
                                             "\xE8\xA7\xA3\xE6\x9E\x90\xE5\xA4\xB1\xE6\x95\x97\xE3\x80\x82"),
                  ok_col, bad_col);
    } else if (s.video_status == 2) {
        StatusRow(true,
                  i18n::t("Parsed successfully.",
                          "\xE8\xA7\xA3\xE6\x9E\x90\xE6\x88\x90\xE5\x8A\x9F\xE3\x80\x82",
                          "\xE8\xA7\xA3\xE6\x9E\x90\xE6\x88\x90\xE5\x8A\x9F\xE3\x80\x82"),
                  ok_col, bad_col);
        if (s.video_result_title[0]) {
            ImGui::TextColored(col::text, "%s", s.video_result_title);
        }
        if (s.video_result_meta[0]) {
            ImGui::TextColored(col::text_dim, "%s", s.video_result_meta);
        }
    } else {
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("No result yet. Enter a link and press Parse.",
                    "\xE5\xB0\x9A\xE6\x97\xA0\xE7\xBB\x93\xE6\x9E\x9C\xE3\x80\x82\xE8\xBE\x93\xE5\x85\xA5\xE9\x93\xBE\xE6\x8E\xA5\xE5\x90\x8E\xE7\x82\xB9\xE5\x87\xBB\xE8\xA7\xA3\xE6\x9E\x90\xE3\x80\x82",
                    "\xE5\xB0\x9A\xE7\x84\xA1\xE7\xB5\x90\xE6\x9E\x9C\xE3\x80\x82\xE8\xBC\xB8\xE5\x85\xA5\xE9\x80\xA3\xE7\xB5\x90\xE5\xBE\x8C\xE9\xBB\x9E\xE6\x93\x8A\xE8\xA7\xA3\xE6\x9E\x90\xE3\x80\x82"));
    }
    ImGui::PopFont();

    if (s.video_status == 2 && s.video_result_url[0]) {
        ImGui::Dummy(ImVec2(0, S(6.f)));
        // 只读多行 —— 把直链塞进去给用户选/复制。
        NLInputTextMultiline("##video_url", nullptr, s.video_result_url,
                             sizeof(s.video_result_url),
                             ImGui::GetContentRegionAvail().x - S(14.f), S(70.f),
                             ImGuiInputTextFlags_ReadOnly);

        ImGui::Dummy(ImVec2(0, S(4.f)));
        float bw = ImGui::GetContentRegionAvail().x - S(14.f);
        ImGui::PushFont(font_medium);
        const char* lbl = (s.video_copy_toast_sec > 0.f)
            ? i18n::t("Copied!",
                      "\xE5\xB7\xB2\xE5\xA4\x8D\xE5\x88\xB6\xEF\xBC\x81",
                      "\xE5\xB7\xB2\xE8\xA4\x87\xE8\xA3\xBD\xEF\xBC\x81")
            : i18n::t("Copy URL",
                      "\xE5\xA4\x8D\xE5\x88\xB6\xE9\x93\xBE\xE6\x8E\xA5",
                      "\xE8\xA4\x87\xE8\xA3\xBD\xE9\x80\xA3\xE7\xB5\x90");
        if (NLButton(lbl, bw, S(32.f), /*accent*/true)) {
            s.video_copy_request = true;
            s.video_copy_toast_sec = 1.4f;
        }
        ImGui::PopFont();

        ImGui::PushFont(font_caption);
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("Paste this into a VRChat video player. The link is signed and expires "
                    "in ~2 hours.",
                    "\xE5\x9C\xA8 VRChat \xE8\xA7\x86\xE9\xA2\x91\xE6\x92\xAD\xE6\x94\xBE\xE5\x99\xA8\xE9\x87\x8C\xE7\xB2\x98\xE8\xB4\xB4\xE6\xAD\xA4\xE9\x93\xBE\xE6\x8E\xA5\xE5\x8D\xB3\xE5\x8F\xAF\xEF\xBC\x9B\xE5\xB8\xA6\xE7\xAD\xBE\xE5\x90\x8D\xEF\xBC\x8C\xE5\xA4\xA7\xE6\xA6\x82 2 \xE5\xB0\x8F\xE6\x97\xB6\xE5\x90\x8E\xE5\xA4\xB1\xE6\x95\x88\xE3\x80\x82",
                    "\xE5\x9C\xA8 VRChat \xE5\xBD\xB1\xE7\x89\x87\xE6\x92\xAD\xE6\x94\xBE\xE5\x99\xA8\xE8\xB2\xBC\xE4\xB8\x8A\xE6\xAD\xA4\xE9\x80\xA3\xE7\xB5\x90\xE5\x8D\xB3\xE5\x8F\xAF\xEF\xBC\x9B\xE5\xB8\xB6\xE7\xB0\xBD\xE5\x90\x8D\xEF\xBC\x8C\xE7\xB4\x84 2 \xE5\xB0\x8F\xE6\x99\x82\xE5\xBE\x8C\xE5\xA4\xB1\xE6\x95\x88\xE3\x80\x82"));
        ImGui::PopFont();
    }
    CardEnd();
}

static void DrawSettings(State& s) {
    SectionTitle(i18n::t("APPEARANCE", "外观", "外觀"));
    CardBegin("##card_app");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("Language", "语言", "語言"));
    ImGui::PopFont();
    static const char* lang_labels[] = { "English", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87", "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87" };
    int li = (int)s.language;
    if (NLCombo("##language", &li, lang_labels, IM_ARRAYSIZE(lang_labels))) {
        s.language = (i18n::Lang)li;
    }
    CardEnd();

    SectionTitle(i18n::t("BEHAVIOR", "行为", "行為"));
    CardBegin("##card_behavior");
    NLToggle(i18n::t("Minimize to tray on close",
                     "关闭时最小化到托盘后台运行",
                     "關閉時最小化到系統匣背景執行"),
             &s.minimize_to_tray);
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("When off, closing the window quits the app.",
                "关闭后点 X 会直接退出;开启则藏到托盘继续跑歌词/OSC。",
                "關閉後按 X 會直接結束;開啟則藏到系統匣繼續執行。"));
    ImGui::PopFont();
    CardEnd();

    SectionTitle("OSC");
    CardBegin("##card_osc");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("Host", "主机", "主機"));
    ImGui::PopFont();
    NLInputText("##host", nullptr, s.osc_host, sizeof(s.osc_host));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("Port", "端口", "連接埠"));
    ImGui::PopFont();
    // 输入框,默认 9000;只允许 1–65535。滑条拖 65535 档位太难用了。
    if (s.osc_port < 1 || s.osc_port > 65535) s.osc_port = 9000;
    NLInputInt("##port", "9000", &s.osc_port, 1, 65535);
    NLSliderInt(i18n::t("Rate limit (ms)", "速率限制 (毫秒)", "速率限制 (毫秒)"),
                &s.rate_limit_ms, 500, 3000);
    CardEnd();

    SectionTitle(i18n::t("LYRICS", "歌词", "歌詞"));
    CardBegin("##card_lyr");
    static const char* providers_en[] = { "Netease only", "Netease then LRCLib", "LRCLib only" };
    static const char* providers_sc[] = { "仅网易云",      "网易云然后 LRCLib",   "仅 LRCLib" };
    static const char* providers_tc[] = { "僅網易雲",      "網易雲然後 LRCLib",   "僅 LRCLib" };
    const char** providers =
        s.language == i18n::Lang::SC ? providers_sc :
        s.language == i18n::Lang::TC ? providers_tc : providers_en;

    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Provider priority", "提供方优先级", "提供方優先順序"));
    ImGui::PopFont();
    if (s.lyrics_provider < 0 || s.lyrics_provider > 2) s.lyrics_provider = 0;
    NLCombo("##provider", &s.lyrics_provider, providers, 3);
    NLToggle(i18n::t("Include translation",  "包含翻译",        "包含翻譯"),         &s.include_translation);
    NLToggle(i18n::t("Strip metadata tags",  "去除元数据标签",   "去除中繼資料標籤"), &s.strip_metadata_tags);
    CardEnd();

    SectionTitle(i18n::t("FORMAT TEMPLATES", "格式模板", "格式模板"));
    CardBegin("##card_fmt");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("When lyrics are available", "有歌词时", "有歌詞時"));
    ImGui::PopFont();
    NLInputTextMultiline("##fmt_l", nullptr, s.fmt_lyrics, sizeof(s.fmt_lyrics),
                         ImGui::GetContentRegionAvail().x, S(44.f));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("When no lyrics", "无歌词时", "無歌詞時"));
    ImGui::PopFont();
    NLInputText("##fmt_nl", nullptr, s.fmt_no_lyrics, sizeof(s.fmt_no_lyrics));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("When paused", "暂停时", "暫停時"));
    ImGui::PopFont();
    NLInputText("##fmt_p", nullptr, s.fmt_paused, sizeof(s.fmt_paused));
    CardEnd();
}

void Draw(State& s, int win_w, int win_h) {
    i18n::current = s.language;
    TickThemeTransition(ImGui::GetIO().DeltaTime);

    const float top_h = S(41.f);
    const float sb_w  = S(150.f);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)win_w, top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_titlebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##titlebar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    DrawTitleBarContent(s, win_w);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(ImVec2(0.f, top_h));
    ImGui::SetNextWindowSize(ImVec2(sb_w, (float)win_h - top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_sidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, S(14.f)));
    ImGui::Begin("##sidebar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

    Tab clicked_tab = s.current_tab;
    g_sidebar_tab_count = 0;
    if (SidebarTab(i18n::t("Lyrics",   "歌词", "歌詞"), icons::DrawMusic,     s.current_tab == Tab::Lyrics))   clicked_tab = Tab::Lyrics;
    if (SidebarTab(i18n::t("Activity", "应用", "應用"), icons::DrawAppWindow, s.current_tab == Tab::Activity)) clicked_tab = Tab::Activity;
    if (SidebarTab(i18n::t("Audio",    "\xE9\x9F\xB3\xE9\xA2\x91", "\xE9\x9F\xB3\xE9\xA0\xBB"), icons::DrawSpeaker, s.current_tab == Tab::Audio)) clicked_tab = Tab::Audio;
    if (SidebarTab(i18n::t("Video",    "\xE8\xA7\x86\xE9\xA2\x91", "\xE5\xBD\xB1\xE7\x89\x87"), icons::DrawVideo,   s.current_tab == Tab::Video)) clicked_tab = Tab::Video;
    if (SidebarTab(i18n::t("Settings", "设置", "設定"), icons::DrawGear,      s.current_tab == Tab::Settings)) clicked_tab = Tab::Settings;

    DrawSidebarIndicator((int)s.current_tab);

    if (clicked_tab != s.current_tab) {
        s.last_tab = s.current_tab;
        s.current_tab = clicked_tab;
        s.tab_transition = 0.f;
    }

    const float footer_h = S(86.f);
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz  = ImGui::GetWindowSize();
    ImVec2 f0(wpos.x, wpos.y + wsz.y - footer_h);
    ImDrawList* dl_fg = ImGui::GetForegroundDrawList();
    dl_fg->AddRectFilled(ImVec2(f0.x, f0.y), ImVec2(f0.x + wsz.x, f0.y + S(1.f)), U32(col::stroke));

    ImVec2 av_c(f0.x + S(30.f), f0.y + S(34.f));

    float disc_r = S(22.f);
    if (s.np_playing) s.cover_angle += ImGui::GetIO().DeltaTime * 0.7f;
    while (s.cover_angle > 6.2831853f) s.cover_angle -= 6.2831853f;
    s.cover_swap_anim = ImMin(1.f, s.cover_swap_anim + ImGui::GetIO().DeltaTime / 0.35f);

    if (s.cover_srv) {
        float ang = s.cover_angle;
        float ca = std::cos(ang), sa = std::sin(ang);
        ImVec2 base[4] = { {-disc_r,-disc_r}, {disc_r,-disc_r}, {disc_r,disc_r}, {-disc_r,disc_r} };
        ImVec2 p[4];
        for (int i = 0; i < 4; ++i) {
            p[i] = ImVec2(av_c.x + base[i].x * ca - base[i].y * sa,
                          av_c.y + base[i].x * sa + base[i].y * ca);
        }
        ImVec2 uv[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
        ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f,1.f,1.f, s.cover_swap_anim));

        if (s.cover_srv_prev && s.cover_swap_anim < 1.f) {
            ImU32 tint_prev = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f,1.f,1.f, 1.f - s.cover_swap_anim));
            ImVec2 pp[4];
            for (int i = 0; i < 4; ++i) {
                pp[i] = ImVec2(av_c.x + base[i].x, av_c.y + base[i].y);
            }
            dl_fg->AddImageQuad((ImTextureID)s.cover_srv_prev, pp[0], pp[1], pp[2], pp[3],
                                uv[0], uv[1], uv[2], uv[3], tint_prev);
        }
        dl_fg->AddImageQuad((ImTextureID)s.cover_srv, p[0], p[1], p[2], p[3],
                            uv[0], uv[1], uv[2], uv[3], tint);
        dl_fg->AddCircleFilled(av_c, S(3.5f), U32(col::bg_sidebar), 16);
    } else {
        dl_fg->AddCircleFilled(av_c, S(18.f), U32(col::bg_card), 24);
        icons::DrawMusic(dl_fg, av_c, S(18.f), U32(col::accent));
    }

    auto truncate_utf8 = [](const char* src, int max_chars, char* out, size_t out_cap) {
        size_t i = 0, chars = 0;
        while (src[i] && chars < (size_t)max_chars && i + 4 < out_cap) {
            uint8_t c = (uint8_t)src[i];
            int seq = (c < 0x80) ? 1 : (c < 0xC0) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            for (int k = 0; k < seq && src[i]; ++k) out[i] = src[i], ++i;
            chars += (c < 0x80) ? 1 : 2;  // CJK roughly double-wide
        }
        out[i] = 0;
    };

    if (s.np_detected) {
        char tbuf[128], abuf[128];
        truncate_utf8(s.np_title,  12, tbuf, sizeof(tbuf));
        truncate_utf8(s.np_artist, 14, abuf, sizeof(abuf));
        ImGui::PushFont(font_body);
        dl_fg->AddText(ImVec2(f0.x + S(62.f), f0.y + S(20.f)), U32(col::text), tbuf);
        ImGui::PopFont();
        ImGui::PushFont(font_caption);
        dl_fg->AddText(ImVec2(f0.x + S(62.f), f0.y + S(40.f)), U32(col::text_dim), abuf);
        ImGui::PopFont();
    } else {
        ImGui::PushFont(font_body);
        dl_fg->AddText(ImVec2(f0.x + S(62.f), f0.y + S(20.f)), U32(col::text_dim),
                       i18n::t("No track", "无曲目", "無曲目"));
        ImGui::PopFont();
    }

    ImGui::PushFont(font_caption);
    ImU32 status_col = s.service_running
        ? U32(ImVec4(0.40f, 0.86f, 0.50f, 1.f))   // green
        : U32(col::text_dim);                     // gray
    dl_fg->AddText(ImVec2(f0.x + S(12.f), f0.y + footer_h - S(18.f)),
                   status_col,
                   s.service_running
                     ? i18n::t("\xE2\x97\x8F Service ON",  "\xE2\x97\x8F \xE6\x9C\x8D\xE5\x8A\xA1\xE5\xBC\x80\xE5\x90\xAF", "\xE2\x97\x8F \xE6\x9C\x8D\xE5\x8B\x99\xE9\x96\x8B\xE5\x95\x9F")
                     : i18n::t("\xE2\x97\x8B Service OFF", "\xE2\x97\x8B \xE6\x9C\x8D\xE5\x8A\xA1\xE5\x85\xB3\xE9\x97\xAD", "\xE2\x97\x8B \xE6\x9C\x8D\xE5\x8B\x99\xE9\x97\x9C\xE9\x96\x89"));
    const char* ver = "v3.3";
    ImVec2 vsz = ImGui::CalcTextSize(ver);
    dl_fg->AddText(ImVec2(f0.x + wsz.x - vsz.x - S(12.f), f0.y + footer_h - S(18.f)),
                   U32(col::text_dim), ver);
    ImGui::PopFont();

    dl_fg->AddRectFilled(ImVec2(wpos.x + sb_w - S(1.f), top_h),
                         ImVec2(wpos.x + sb_w, wpos.y + wsz.y), U32(col::stroke));

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    s.tab_transition = ImMin(1.f, s.tab_transition + ImGui::GetIO().DeltaTime / 0.22f);
    float t  = EaseOutCubic(s.tab_transition);
    float dx = (1.f - t) * S(24.f);
    float a  = t;

    // 让 CardBegin 能看到当前帧的 tab 过渡值,做卡片 staggered slide-in。
    g_tab_anim_t = s.tab_transition;
    g_card_index = 0;

    ImGui::SetNextWindowPos(ImVec2(sb_w, top_h));
    ImGui::SetNextWindowSize(ImVec2((float)win_w - sb_w, (float)win_h - top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_content);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.f) + dx, S(22.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    // 内容区滚轮自己做惯性,关掉 ImGui 默认瞬时跳变 + 默认滚动条。
    ImGui::Begin("##content", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar);

    // ---- 惯性滚动:吃滚轮 → 速度,每帧指数衰减 → 平滑 ScrollY ----
    // 切 tab 时内容高度会变,把目标钳到新 max 并清速度,避免飞出。
    {
        ImGuiWindow* cwin = ImGui::GetCurrentWindow();
        static int   s_last_tab = -1;
        static float s_scroll_vel = 0.f;
        static float s_scroll_tgt = 0.f;
        static bool  s_scroll_inited = false;

        int tab_i = (int)s.current_tab;
        float max_y = cwin ? cwin->ScrollMax.y : 0.f;
        float cur_y = cwin ? cwin->Scroll.y : 0.f;

        if (!s_scroll_inited || tab_i != s_last_tab) {
            s_scroll_inited = true;
            s_last_tab = tab_i;
            s_scroll_vel = 0.f;
            s_scroll_tgt = cur_y;
        }

        // 滚轮:向上为正(ImGui 约定)。乘内容区高度比例,触控板一划也有手感。
        ImGuiIO& io = ImGui::GetIO();
        float wheel = io.MouseWheel;
        // 只在内容区 hover 时接滚轮;combo 弹层打开时不抢。
        bool content_hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (content_hov && wheel != 0.f && max_y > 0.f) {
            float step = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
            // 触控板连续小值 / 鼠标离散大值都兼容:直接累速度
            s_scroll_vel -= wheel * step * 14.f;
            // 同步目标到当前位置,避免旧目标把速度拉回去
            s_scroll_tgt = cur_y;
        }

        float dt = io.DeltaTime;
        if (dt > 0.05f) dt = 0.05f;

        // 指数衰减摩擦系数;速度积分到目标,再 ease 到 ScrollY
        const float friction = 10.f; // 越大停得越快
        s_scroll_vel *= std::exp(-friction * dt);
        if (std::fabs(s_scroll_vel) < 0.5f) s_scroll_vel = 0.f;

        s_scroll_tgt += s_scroll_vel * dt;
        if (s_scroll_tgt < 0.f)      { s_scroll_tgt = 0.f;      s_scroll_vel = 0.f; }
        if (s_scroll_tgt > max_y)    { s_scroll_tgt = max_y;    s_scroll_vel = 0.f; }

        // 位置向目标插值 —— 比直接 SetScrollY(target) 更丝滑
        float follow = 1.f - std::exp(-18.f * dt);
        float next_y = cur_y + (s_scroll_tgt - cur_y) * follow;
        if (std::fabs(next_y - s_scroll_tgt) < 0.25f) next_y = s_scroll_tgt;
        if (cwin && max_y > 0.f) {
            ImGui::SetScrollY(next_y);
        } else if (cwin) {
            ImGui::SetScrollY(0.f);
            s_scroll_tgt = 0.f;
            s_scroll_vel = 0.f;
        }

        // 自绘细滚动条(仅内容可滚时)
        if (cwin && max_y > 1.f) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wpos = ImGui::GetWindowPos();
            ImVec2 wsz  = ImGui::GetWindowSize();
            float track_w = S(4.f);
            float pad    = S(4.f);
            float track_x0 = wpos.x + wsz.x - track_w - pad;
            float track_y0 = wpos.y + pad;
            float track_y1 = wpos.y + wsz.y - pad;
            float track_h  = track_y1 - track_y0;
            // 可视比例
            float view_h = cwin->InnerRect.GetHeight();
            float content_h = view_h + max_y;
            float grab_h = ImClamp(track_h * (view_h / ImMax(1.f, content_h)), S(18.f), track_h);
            float tnorm = (max_y > 0.f) ? (next_y / max_y) : 0.f;
            float grab_y = track_y0 + (track_h - grab_h) * tnorm;
            // 轨道几乎透明,grab 用 dim 色
            dl->AddRectFilled(ImVec2(track_x0, track_y0),
                              ImVec2(track_x0 + track_w, track_y1),
                              U32(col::stroke, 0.25f), track_w * 0.5f);
            dl->AddRectFilled(ImVec2(track_x0, grab_y),
                              ImVec2(track_x0 + track_w, grab_y + grab_h),
                              U32(col::text_dim, 0.55f), track_w * 0.5f);
        }
    }

    switch (s.current_tab) {
        case Tab::Lyrics:   DrawLyrics(s);   break;
        case Tab::Activity: DrawActivity(s); break;
        case Tab::Audio:    DrawAudio(s);    break;
        case Tab::Video:    DrawVideoTab(s); break;
        case Tab::Settings: DrawSettings(s); break;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

}
