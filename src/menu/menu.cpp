#include "menu.h"
#include "style.h"
#include "i18n/i18n.h"
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
    ImVec2 lsz = ImGui::CalcTextSize(label);
    win->DrawList->AddText(ImVec2(bb.Min.x, bb.Min.y + (row_h - lsz.y) * 0.5f), txt, label);

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
    win->DrawList->AddText(bb.Min, U32(col::text_dim), label);
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

static void SectionTitle(const char* upper_title) {
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::PushFont(font_caption);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(p, U32(col::text_caption), upper_title);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(12.f)));
}

// 用 ImDrawList 的通道切分实现卡片背景,不用 BeginChild —— child window 在
// 这个版本的 ImGui 上会拦截鼠标事件导致按钮点不动(踩过这个坑)。
static ImVec2 g_card_start;

static void CardBegin(const char* /*id*/) {
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
    dl->AddRectFilled(r_min, r_max, U32(col::bg_card), S(8.f));
    dl->ChannelsMerge();

    ImGui::Dummy(ImVec2(0, S(10.f)));
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

static bool SidebarTab(const char* label, void(*icon)(ImDrawList*, ImVec2, float, ImU32), bool selected) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGuiID id = win->GetID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(34.f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImRect bb(p, ImVec2(p.x + w, p.y + h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    float t_sel = Anim(id, selected);
    float t_hov = Anim(win->GetID((const void*)((uintptr_t)id ^ 2u)), hovered || selected);

    if (t_hov > 0.01f && !selected)
        win->DrawList->AddRectFilled(bb.Min, bb.Max, U32(col::bg_hover, t_hov * 0.45f), 0.f);

    if (t_sel > 0.01f)
        win->DrawList->AddRectFilled(
            ImVec2(bb.Min.x, bb.Min.y + S(8.f)),
            ImVec2(bb.Min.x + S(3.f), bb.Max.y - S(8.f)),
            U32(col::accent, t_sel), S(1.5f));

    ImU32 fg = Mix(col::text_dim, col::text, ImMax(t_sel, t_hov * 0.6f));
    icon(win->DrawList, ImVec2(bb.Min.x + S(22.f), (bb.Min.y + bb.Max.y) * 0.5f), S(16.f), fg);

    ImGui::PushFont(font_body);
    ImVec2 lsz = ImGui::CalcTextSize(label);
    win->DrawList->AddText(ImVec2(bb.Min.x + S(40.f), (bb.Min.y + bb.Max.y - lsz.y) * 0.5f), fg, label);
    ImGui::PopFont();
    return pressed;
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
        ImGui::TextColored(col::text_dim, "%s %s",
                           i18n::t("Track ID:", "曲目 ID:", "曲目 ID:"),
                           s.np_ncm_id[0] ? s.np_ncm_id : "-");
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
        ImGui::TextColored(col::text_dim, "Track ID: -");
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("Open netease cloud music + inflink-rs plugin",
                    "请打开网易云音乐 + inflink-rs 插件",
                    "請開啟網易雲音樂 + inflink-rs 插件"));
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
    float btn_w = ImGui::GetContentRegionAvail().x;
    float btn_h = S(36.f);
    ImGui::PushStyleColor(ImGuiCol_Button,
        s.service_running ? ImVec4(0.65f, 0.25f, 0.30f, 1.f) : col::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        s.service_running ? ImVec4(0.78f, 0.30f, 0.35f, 1.f) :
        ImVec4(col::accent.x * 1.15f, col::accent.y * 1.15f, col::accent.z * 1.15f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        s.service_running ? ImVec4(0.55f, 0.20f, 0.25f, 1.f) :
        ImVec4(col::accent.x * 0.85f, col::accent.y * 0.85f, col::accent.z * 0.85f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.07f, 0.10f, 1.f));
    ImGui::PushFont(font_medium);
    if (ImGui::Button(btn_label, ImVec2(btn_w, btn_h))) s.service_running = !s.service_running;
    ImGui::PopFont();
    ImGui::PopStyleColor(4);

    ImGui::Dummy(ImVec2(0, S(4.f)));
    NLToggle(i18n::t("Send while paused", "暂停时仍发送", "暫停時仍傳送"), &s.send_while_paused);
    CardEnd();
}

static void DrawActivity(State& s) {
    SectionTitle(i18n::t("FOREGROUND APP", "前台应用", "前台應用"));
    CardBegin("##card_act");
    ImGui::PushFont(font_title);
    if (s.foreground_app[0]) {
        ImGui::TextColored(col::text, "\xF0\x9F\x8E\xAE %s", s.foreground_app);
    } else {
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("(no foreground app)", "(无前台应用)", "(無前台應用)"));
    }
    ImGui::PopFont();
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("The application that currently owns keyboard focus on your system.",
                "当前键盘焦点所在的应用。",
                "目前鍵盤焦點所在的應用程式。"));
    ImGui::PopFont();
    CardEnd();

    SectionTitle(i18n::t("BROADCAST", "广播", "廣播"));
    CardBegin("##card_act_send");
    NLToggle(i18n::t("Append to chatbox", "附加到 chatbox", "附加到 chatbox"),
             &s.show_foreground_app);
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Prefix the chatbox message with the foreground app name.",
                "在 chatbox 消息前加上当前前台应用名。",
                "在 chatbox 訊息前加上目前前台應用程式名稱。"));
    ImGui::PopFont();
    if (s.show_foreground_app && s.foreground_app[0]) {
        ImGui::Dummy(ImVec2(0, S(6.f)));
        ImGui::PushFont(font_body);
        ImGui::TextColored(col::accent, "\xF0\x9F\x8E\xAE %s \xC2\xB7 ...", s.foreground_app);
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
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##language", &li, lang_labels, IM_ARRAYSIZE(lang_labels))) {
        s.language = (i18n::Lang)li;
    }
    CardEnd();

    SectionTitle("OSC");
    CardBegin("##card_osc");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("Host", "主机", "主機"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##host", s.osc_host, sizeof(s.osc_host));
    NLSliderInt(i18n::t("Port", "端口", "連接埠"), &s.osc_port, 1, 65535);
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
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##provider", &s.lyrics_provider, providers, 3);
    NLToggle(i18n::t("Include translation",  "包含翻译",        "包含翻譯"),         &s.include_translation);
    NLToggle(i18n::t("Strip metadata tags",  "去除元数据标签",   "去除中繼資料標籤"), &s.strip_metadata_tags);
    CardEnd();

    SectionTitle(i18n::t("FORMAT TEMPLATES", "格式模板", "格式模板"));
    CardBegin("##card_fmt");
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("When lyrics are available", "有歌词时", "有歌詞時"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextMultiline("##fmt_l", s.fmt_lyrics, sizeof(s.fmt_lyrics),
                              ImVec2(-FLT_MIN, S(44.f)));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("When no lyrics", "无歌词时", "無歌詞時"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##fmt_nl", s.fmt_no_lyrics, sizeof(s.fmt_no_lyrics));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("When paused", "暂停时", "暫停時"));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##fmt_p", s.fmt_paused, sizeof(s.fmt_paused));
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
    if (SidebarTab(i18n::t("Lyrics",   "歌词", "歌詞"), icons::DrawMusic,     s.current_tab == Tab::Lyrics))   clicked_tab = Tab::Lyrics;
    if (SidebarTab(i18n::t("Activity", "应用", "應用"), icons::DrawAppWindow, s.current_tab == Tab::Activity)) clicked_tab = Tab::Activity;
    if (SidebarTab(i18n::t("Settings", "设置", "設定"), icons::DrawGear,      s.current_tab == Tab::Settings)) clicked_tab = Tab::Settings;

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
    const char* ver = "v0.1";
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

    ImGui::SetNextWindowPos(ImVec2(sb_w, top_h));
    ImGui::SetNextWindowSize(ImVec2((float)win_w - sb_w, (float)win_h - top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_content);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.f) + dx, S(22.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::Begin("##content", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    switch (s.current_tab) {
        case Tab::Lyrics:   DrawLyrics(s);   break;
        case Tab::Activity: DrawActivity(s); break;
        case Tab::Settings: DrawSettings(s); break;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

}
