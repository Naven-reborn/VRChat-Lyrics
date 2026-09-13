#include "menu.h"
#include "style.h"
#include "lyric_motion.h"
#include "ui_motion.h"
#include "title_glitch.h"
#include "i18n/i18n.h"
#include "util/foreground.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace menu {

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static float EaseOutCubic(float t) { float u = 1.f - t; return 1.f - u * u * u; }

static float AnimateValue(ImGuiID id, float target, float duration = .20f) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID from_id=ImHashStr("motion-from",0,id);
    const ImGuiID target_id=ImHashStr("motion-target",0,id);
    const ImGuiID elapsed_id=ImHashStr("motion-time",0,id);
    motion::Tween tween;
    tween.value=st->GetFloat(id,target);
    tween.from=st->GetFloat(from_id,tween.value);
    tween.target=st->GetFloat(target_id,tween.value);
    tween.elapsed=st->GetFloat(elapsed_id,duration);
    tween.Tick(target,ImGui::GetIO().DeltaTime,duration,interface_motion_enabled);
    st->SetFloat(id,tween.value);st->SetFloat(from_id,tween.from);
    st->SetFloat(target_id,tween.target);st->SetFloat(elapsed_id,tween.elapsed);
    return tween.value;
}
static float Anim(ImGuiID id, bool target, float speed = 14.f) {
    return AnimateValue(id,target ? 1.f : 0.f,ImClamp(2.8f/speed,.10f,.24f));
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

// ---------------- Format builder ----------------

const char* FmtFieldLabel(int field_id) {
    switch (field_id) {
        case 0: return i18n::t("Status",  "状态",   "狀態");
        case 1: return i18n::t("Icon",    "图标",   "圖示");
        case 2: return i18n::t("Title",   "歌名",   "歌名");
        case 3: return i18n::t("Artist",  "艺人",   "藝人");
        case 4: return i18n::t("Progress","进度",   "進度");
        case 5: return i18n::t("Lyrics",  "歌词",   "歌詞");
        default: return "?";
    }
}

const char* FmtSepLiteral(int sep_style) {
    switch (sep_style) {
        case 1: return " \xC2\xB7 "; // ·
        case 2: return " | ";
        case 3: return " ";
        case 0:
        default: return " - ";
    }
}

// 把 builder 规范化:去重、补缺、钳范围。
static void NormalizeFmtBuilder(State::FmtBuilder& b, bool for_lyrics) {
    bool seen[State::kFmtFieldCount] = {};
    unsigned char tmp[State::kFmtOrderCap];
    int n = 0;
    for (int i = 0; i < State::kFmtOrderCap; ++i) {
        unsigned char id = b.order[i];
        if (id == 0xFF) break;
        if (id >= State::kFmtFieldCount) continue;
        if (seen[id]) continue;
        seen[id] = true;
        tmp[n++] = id;
    }
    // 缺的字段补到末尾(默认关闭)
    for (int id = 0; id < State::kFmtFieldCount; ++id) {
        if (!seen[id] && n < State::kFmtOrderCap - 1) {
            tmp[n++] = (unsigned char)id;
            // 没在 order 里出现过的,enabled 保持现有;若是全新默认则 for_lyrics 决定
        }
    }
    for (int i = 0; i < n; ++i) b.order[i] = tmp[i];
    for (int i = n; i < State::kFmtOrderCap; ++i) b.order[i] = 0xFF;
    if (b.layout != 0 && b.layout != 1) b.layout = for_lyrics ? 1 : 0;
    if (b.sep_style < 0 || b.sep_style > 3) b.sep_style = 0;
}

// 默认 builder
static State::FmtBuilder DefaultFmtWithLyrics() {
    State::FmtBuilder b{};
    // status, icon, title, artist, lyrics
    unsigned char o[] = { 0, 1, 2, 3, 5, 0xFF, 0xFF, 0xFF };
    std::memcpy(b.order, o, sizeof(o));
    b.enabled[0] = true;  // status
    b.enabled[1] = true;  // icon
    b.enabled[2] = true;  // title
    b.enabled[3] = true;  // artist
    b.enabled[4] = false; // progress
    b.enabled[5] = true;  // lyrics
    b.layout = 1;
    b.sep_style = 0;
    return b;
}
static State::FmtBuilder DefaultFmtNoLyrics() {
    State::FmtBuilder b{};
    unsigned char o[] = { 0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF };
    std::memcpy(b.order, o, sizeof(o));
    b.enabled[0] = true;
    b.enabled[1] = true;
    b.enabled[2] = true;
    b.enabled[3] = true;
    b.enabled[4] = true;  // progress
    b.enabled[5] = false; // lyrics off
    b.layout = 0;
    b.sep_style = 0;
    return b;
}

static bool BuilderIsDefaultWithLyrics(const State::FmtBuilder& b) {
    auto d = DefaultFmtWithLyrics();
    return std::memcmp(&b, &d, sizeof(b)) == 0;
}
static bool BuilderIsDefaultNoLyrics(const State::FmtBuilder& b) {
    auto d = DefaultFmtNoLyrics();
    return std::memcmp(&b, &d, sizeof(b)) == 0;
}

// 从旧模板字符串粗解析 → builder(尽力而为,识别常见 token)
static State::FmtBuilder ParseLegacyTemplate(const char* tmpl, bool expect_lyrics) {
    State::FmtBuilder b = expect_lyrics ? DefaultFmtWithLyrics() : DefaultFmtNoLyrics();
    if (!tmpl || !tmpl[0]) return b;

    std::string t(tmpl);
    // layout: 有换行 → 两行
    b.layout = (t.find('\n') != std::string::npos) ? 1 : 0;

    // sep
    if (t.find(" \xC2\xB7 ") != std::string::npos || t.find(" · ") != std::string::npos)
        b.sep_style = 1;
    else if (t.find(" | ") != std::string::npos)
        b.sep_style = 2;
    else if (t.find(" - ") != std::string::npos)
        b.sep_style = 0;
    else
        b.sep_style = 3;

    // 字段出现顺序:按第一次出现的位置排序
    struct Hit { int id; size_t pos; };
    std::vector<Hit> hits;
    auto add = [&](int id, const char* token) {
        size_t p = t.find(token);
        if (p != std::string::npos) hits.push_back({ id, p });
    };
    add(0, "{status}");
    add(1, "{mic}");
    add(1, "{icon}"); // 新名字
    add(2, "{name}");
    add(2, "{title}");
    add(3, "{artist}");
    add(4, "{progress}");
    add(4, "{time}");
    add(5, "{lyrics}");

    if (!hits.empty()) {
        std::sort(hits.begin(), hits.end(),
                  [](const Hit& a, const Hit& b) { return a.pos < b.pos; });
        bool seen[State::kFmtFieldCount] = {};
        int n = 0;
        for (auto& h : hits) {
            if (h.id < 0 || h.id >= State::kFmtFieldCount) continue;
            if (seen[h.id]) continue;
            seen[h.id] = true;
            b.order[n++] = (unsigned char)h.id;
        }
        for (int id = 0; id < State::kFmtFieldCount; ++id) {
            if (!seen[id] && n < State::kFmtOrderCap - 1)
                b.order[n++] = (unsigned char)id;
        }
        for (int i = n; i < State::kFmtOrderCap; ++i) b.order[i] = 0xFF;
        for (int id = 0; id < State::kFmtFieldCount; ++id)
            b.enabled[id] = seen[id];
    }
    NormalizeFmtBuilder(b, expect_lyrics);
    return b;
}

void MigrateLegacyFormats(State& s) {
    // 仅当 builder 仍是默认、且旧字符串存在时迁移,避免覆盖用户已调的 builder。
    if (BuilderIsDefaultWithLyrics(s.fmt_with_lyrics) && s.fmt_lyrics[0]) {
        s.fmt_with_lyrics = ParseLegacyTemplate(s.fmt_lyrics, true);
    }
    if (BuilderIsDefaultNoLyrics(s.fmt_without_lyrics) && s.fmt_no_lyrics[0]) {
        // 旧 paused 和 no_lyrics 通常一样;优先 no_lyrics 字符串
        s.fmt_without_lyrics = ParseLegacyTemplate(s.fmt_no_lyrics, false);
    } else if (BuilderIsDefaultNoLyrics(s.fmt_without_lyrics) && s.fmt_paused[0]) {
        s.fmt_without_lyrics = ParseLegacyTemplate(s.fmt_paused, false);
    }
    NormalizeFmtBuilder(s.fmt_with_lyrics, true);
    NormalizeFmtBuilder(s.fmt_without_lyrics, false);
}

// 字段值解析
static std::string FieldValue(int id, const State& s, const char* status_prefix,
                              bool playing, const char* lyrics_line) {
    switch (id) {
        case 0: { // status — 去掉末尾 " · "
            std::string st = status_prefix ? status_prefix : "";
            if (st.size() >= 4 &&
                st.compare(st.size() - 4, 4, " \xC2\xB7 ") == 0)
                st.resize(st.size() - 4);
            return st;
        }
        case 1: // icon
            return playing ? "\xE2\x96\xB6\xEF\xB8\x8F"  // ▶
                           : "\xE2\x8F\xB8\xEF\xB8\x8F"; // ⏸
        case 2: return s.np_title;
        case 3: return s.np_artist;
        case 4: {
            int p = s.np_pos_ms / 1000, d = s.np_dur_ms / 1000;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%d:%02d / %d:%02d]",
                          p / 60, p % 60, d / 60, d % 60);
            return buf;
        }
        case 5: {
            if (!lyrics_line || !lyrics_line[0]) return {};
            // 🎤 + 歌词
            std::string out = "\xF0\x9F\x8E\xA4 ";
            out += lyrics_line;
            return out;
        }
        default: return {};
    }
}

// 字段是否需要换行(两行模式下:lyrics / progress 单独成行)
static bool FieldWantsNewline(int id) {
    return id == 4 || id == 5;
}

std::string RenderChatbox(const State& s, const char* status_prefix,
                          bool playing, bool has_lyrics_line,
                          const char* lyrics_line) {
    const State::FmtBuilder& b =
        has_lyrics_line ? s.fmt_with_lyrics : s.fmt_without_lyrics;
    const char* sep = FmtSepLiteral(b.sep_style);

    std::string line1, line2;
    bool first1 = true, first2 = true;

    auto append = [&](std::string& line, bool& first, const std::string& piece, bool use_sep) {
        if (piece.empty()) return;
        if (!first && use_sep) line += sep;
        else if (!first) line += ' ';
        line += piece;
        first = false;
    };

    for (int i = 0; i < State::kFmtOrderCap; ++i) {
        unsigned char id = b.order[i];
        if (id == 0xFF) break;
        if (id >= State::kFmtFieldCount) continue;
        if (!b.enabled[id]) continue;
        // 有歌词场景下如果 lyrics 字段开了但当前行空,跳过
        if (id == 5 && (!lyrics_line || !lyrics_line[0])) continue;

        std::string val = FieldValue(id, s, status_prefix, playing, lyrics_line);
        if (val.empty()) continue;

        bool newline = (b.layout == 1) && FieldWantsNewline(id);
        // status 本身已是完整前缀,后面接空格而不是 sep
        bool use_sep = (id != 0 && id != 1);
        // icon 紧贴后面字段,用空格
        if (id == 1) use_sep = false;

        if (newline) append(line2, first2, val, /*use_sep*/false);
        else         append(line1, first1, val, use_sep);
    }

    if (line1.empty()) return line2;
    if (line2.empty()) return line1;
    return line1 + "\n" + line2;
}

// 预览必须和真实 chatbox 走同一条 RenderChatbox 路径,否则字段顺序/开关会"看起来不同步"。
// 调用方应已把正在编辑的 builder 写进 s.fmt_with_lyrics / s.fmt_without_lyrics。
//
// as_lyrics=true 时:始终展示歌词行(暂停也显示),和实际发送逻辑一致。
// 没有正在播放 / 还没到歌词时,用占位文案,方便设置页也能看到效果。
static std::string PreviewBuilder(const State& s, bool as_lyrics) {
    State tmp = s;
    if (!tmp.np_detected || !tmp.np_title[0]) {
        tmp.np_detected = true;
        tmp.np_playing = true;
        std::snprintf(tmp.np_title, sizeof(tmp.np_title), "Song Title");
        std::snprintf(tmp.np_artist, sizeof(tmp.np_artist), "Artist");
        tmp.np_pos_ms = 65 * 1000;
        tmp.np_dur_ms = 240 * 1000;
    }
    // 有歌词模板预览:即使当前暂停 / 还没到第一句,也给一行占位歌词
    if (as_lyrics && !tmp.np_current_line[0]) {
        std::snprintf(tmp.np_current_line, sizeof(tmp.np_current_line), "lyrics line here");
    }

    std::string prefix = EffectiveStatusPrefix(tmp);
    // 有歌词场景:永远把当前歌词行塞进去(含暂停)。这与 main 里
    // has_line = np_has_lyrics && !current_line.empty() 后仍展示歌词一致。
    const char* lyrics = as_lyrics ? tmp.np_current_line : "";
    return RenderChatbox(tmp, prefix.c_str(), tmp.np_playing, as_lyrics, lyrics);
}

// 卡片内可用宽度前向声明(定义在 Card 区更靠后)
static float ContentW(float extra_shrink = 0.f);

// 单个 builder 的编辑 UI
// 开关:沿用 NLToggle 同款 Anim 滑动圆点。
// 排序:点 ↑/↓ 后,被点那一行先"拎起来"(上浮+阴影),再与邻行滑动换位。
static void DrawFmtBuilderEditor(const char* id, State::FmtBuilder& b, bool for_lyrics, const State& preview_s) {
    ImGui::PushID(id);
    NormalizeFmtBuilder(b, for_lyrics);

    // 布局 + 分隔
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Layout", "布局", "版面"));
    ImGui::PopFont();
    const char* layouts_en[] = { "One line", "Two lines (lyrics/progress below)" };
    const char* layouts_sc[] = { "单行", "两行(歌词/进度换行)" };
    const char* layouts_tc[] = { "單行", "兩行(歌詞/進度換行)" };
    const char** layouts =
        preview_s.language == i18n::Lang::SC ? layouts_sc :
        preview_s.language == i18n::Lang::TC ? layouts_tc : layouts_en;
    NLCombo("##layout", &b.layout, layouts, 2);

    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Separator", "分隔符", "分隔符"));
    ImGui::PopFont();
    const char* seps[] = { " - ", " \xC2\xB7 ", " | ", " (space)" };
    NLCombo("##sep", &b.sep_style, seps, 4);

    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Fields (toggle · reorder)", "字段(开关 · 排序)", "欄位(開關 · 排序)"));
    ImGui::PopFont();

    // ---- 行布局参数 ----
    // 关键:不要用整块 InvisibleButton/Dummy 盖住列表再绝对定位子按钮。
    // 那样会再次出现"点不动"。这里每行正常推进光标;排序动画只做绘制偏移。
    const float row_h = S(34.f);
    const float row_gap = S(4.f);
    const float step = row_h + row_gap;
    const float btn_s = S(28.f);
    const float gap = S(6.f);
    const float pill_w = S(34.f), pill_h = S(18.f);
    const float avail = ContentW();

    int n_fields = 0;
    for (int i = 0; i < State::kFmtOrderCap; ++i) {
        if (b.order[i] == 0xFF) break;
        if (b.order[i] < State::kFmtFieldCount) ++n_fields;
    }
    if (n_fields < 1) n_fields = 1;

    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID k_anim_t   = ImGui::GetID("##swap_t");
    ImGuiID k_anim_a   = ImGui::GetID("##swap_a");
    ImGuiID k_anim_b   = ImGui::GetID("##swap_b");
    ImGuiID k_anim_dir = ImGui::GetID("##swap_dir"); // +1 down / -1 up
    float anim_t = st->GetFloat(k_anim_t, 1.f);
    int   anim_a = st->GetInt(k_anim_a, -1);
    int   anim_b = st->GetInt(k_anim_b, -1);
    int   anim_dir = st->GetInt(k_anim_dir, 0);
    bool animating = (anim_t < 0.999f) && anim_a >= 0 && anim_b >= 0;

    if (animating) {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.05f) dt = 0.05f;
        anim_t += dt / 0.28f;
        if (anim_t >= 1.f) {
            anim_t = 1.f;
            anim_a = anim_b = -1;
            anim_dir = 0;
            st->SetInt(k_anim_a, -1);
            st->SetInt(k_anim_b, -1);
            st->SetInt(k_anim_dir, 0);
        }
        st->SetFloat(k_anim_t, anim_t);
        animating = (anim_t < 0.999f) && anim_a >= 0 && anim_b >= 0;
    }

    auto lift_amount = [](float t, bool is_moving) -> float {
        if (!is_moving) return 0.f;
        if (t < 0.25f) return EaseOutCubic(t / 0.25f);
        if (t < 0.80f) return 1.f;
        return 1.f - EaseOutCubic((t - 0.80f) / 0.20f);
    };
    auto slide_amount = [](float t) -> float {
        if (t < 0.20f) return 0.f;
        if (t >= 0.85f) return 1.f;
        return EaseOutCubic((t - 0.20f) / 0.65f);
    };

    int req_swap_i = -1;
    int req_dir = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 list0 = ImGui::GetCursorScreenPos();

    for (int i = 0; i < n_fields; ++i) {
        unsigned char idf = b.order[i];
        if (idf >= State::kFmtFieldCount) continue;

        float y_off = 0.f;
        float lift = 0.f;
        bool is_mover = false;
        if (animating) {
            float slide = slide_amount(anim_t);
            if (i == anim_a) {
                // 数据已 swap:被点行现在在新 index,从旧位置滑入
                y_off = (-anim_dir) * step * (1.f - slide);
                lift = lift_amount(anim_t, true);
                is_mover = true;
            } else if (i == anim_b) {
                y_off = anim_dir * step * (1.f - slide);
                lift = lift_amount(anim_t, false) * 0.35f;
            }
        }

        // 正常占位行(hit-test 用逻辑位置,不跟着视觉偏移走,保证点得到)
        ImVec2 slot0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(1000 + i);

        // 视觉绘制原点 = 逻辑槽位 + 动画偏移
        ImVec2 row0(slot0.x, slot0.y + y_off - lift * S(6.f));

        // 背景
        bool row_hov = !animating && ImGui::IsMouseHoveringRect(
            slot0, ImVec2(slot0.x + avail, slot0.y + row_h), false);
        if (is_mover && lift > 0.01f) {
            float a = 0.10f + 0.14f * lift;
            dl->AddRectFilled(ImVec2(row0.x + S(2.f), row0.y + S(4.f)),
                              ImVec2(row0.x + avail - S(2.f), row0.y + row_h + S(5.f)),
                              U32(ImVec4(0, 0, 0, a)), S(6.f));
            dl->AddRectFilled(row0, ImVec2(row0.x + avail, row0.y + row_h),
                              U32(col::bg_card), S(6.f));
            dl->AddRect(row0, ImVec2(row0.x + avail, row0.y + row_h),
                        U32(col::accent, 0.35f + 0.35f * lift), S(6.f), 0, S(1.2f));
        } else if (row_hov) {
            dl->AddRectFilled(slot0, ImVec2(slot0.x + avail, slot0.y + row_h),
                              U32(col::bg_hover, 0.55f), S(5.f));
        } else {
            dl->AddRectFilled(slot0, ImVec2(slot0.x + avail, slot0.y + row_h),
                              U32(col::bg_input, 0.35f), S(5.f));
        }

        const bool interactive = !animating;

        // 1) 开关 — 控件放在逻辑槽位上(可点),绘制可跟视觉原点
        bool en = b.enabled[idf];
        {
            ImVec2 hit_min(slot0.x + S(6.f), slot0.y + (row_h - pill_h) * 0.5f);
            ImGui::SetCursorScreenPos(hit_min);
            bool pressed = false;
            if (interactive) pressed = ImGui::InvisibleButton("##en", ImVec2(pill_w, pill_h));
            else ImGui::Dummy(ImVec2(pill_w, pill_h));
            if (pressed) {
                b.enabled[idf] = !en;
                en = b.enabled[idf];
            }
            bool hov_en = ImGui::IsItemHovered() && interactive;

            ImGuiID anim_id = ImGui::GetID("##tog_anim");
            anim_id = (ImGuiID)(anim_id ^ (0xA11Fu * (idf + 1)));
            float t_on = Anim(anim_id, en, 16.f);

            ImVec2 pmin(row0.x + S(6.f), row0.y + (row_h - pill_h) * 0.5f);
            ImVec2 pmax(pmin.x + pill_w, pmin.y + pill_h);
            dl->AddRectFilled(pmin, pmax, Mix(col::bg_input, col::accent, t_on), pill_h * 0.5f);
            if (hov_en) dl->AddRect(pmin, pmax, U32(col::accent, 0.55f), pill_h * 0.5f, 0, S(1.2f));
            float dr = pill_h * 0.5f - S(2.5f);
            float dx = Lerp(pmin.x + dr + S(2.5f), pmax.x - dr - S(2.5f), t_on);
            float dy = (pmin.y + pmax.y) * 0.5f;
            float pr = dr * (0.92f + 0.08f * (hov_en ? 1.f : 0.f));
            dl->AddCircleFilled(ImVec2(dx, dy), pr, Mix(col::dot_off, col::dot_on, t_on), 16);
        }

        // 2) 标签
        {
            const char* lab = FmtFieldLabel(idf);
            ImVec2 lsz = ImGui::CalcTextSize(lab);
            float label_x = slot0.x + S(6.f) + pill_w + S(10.f);
            float label_w = avail - (label_x - slot0.x) - (btn_s * 2 + gap + S(10.f));
            if (label_w < S(40.f)) label_w = S(40.f);
            ImGui::SetCursorScreenPos(ImVec2(label_x, slot0.y + S(2.f)));
            if (interactive && ImGui::InvisibleButton("##lab", ImVec2(label_w, row_h - S(4.f)))) {
                b.enabled[idf] = !b.enabled[idf];
                en = b.enabled[idf];
            } else if (!interactive) {
                ImGui::Dummy(ImVec2(label_w, row_h - S(4.f)));
            }
            dl->AddText(ImVec2(row0.x + S(6.f) + pill_w + S(10.f),
                               row0.y + (row_h - lsz.y) * 0.5f),
                        U32(en ? col::text : col::text_dim), lab);
        }

        // 3) ↑ ↓
        bool can_up = (i > 0);
        bool can_dn = (i + 1 < n_fields);
        float bx = slot0.x + avail - btn_s * 2 - gap - S(4.f);

        auto draw_arrow_btn = [&](const char* bid, float x, bool up, bool enabled) -> bool {
            ImGui::SetCursorScreenPos(ImVec2(x, slot0.y + (row_h - btn_s) * 0.5f));
            bool pressed = false;
            if (interactive && enabled) pressed = ImGui::InvisibleButton(bid, ImVec2(btn_s, btn_s));
            else ImGui::Dummy(ImVec2(btn_s, btn_s));

            bool hov = ImGui::IsItemHovered() && interactive && enabled;
            // 绘制跟视觉行对齐
            ImVec2 bmin(row0.x + (x - slot0.x), row0.y + (row_h - btn_s) * 0.5f);
            ImVec2 bmax(bmin.x + btn_s, bmin.y + btn_s);
            if (hov) {
                dl->AddRectFilled(bmin, bmax, U32(col::bg_hover), S(5.f));
                dl->AddRect(bmin, bmax, U32(col::accent, 0.45f), S(5.f));
            } else {
                dl->AddRectFilled(bmin, bmax, U32(col::bg_input), S(5.f));
            }
            ImVec2 c((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            ImU32 ac = U32(enabled ? col::text : col::text_dim, enabled ? 1.f : 0.35f);
            if (up) {
                dl->AddLine(ImVec2(c.x, c.y + S(4.f)), ImVec2(c.x, c.y - S(4.f)), ac, S(1.6f));
                dl->AddLine(ImVec2(c.x - S(3.5f), c.y - S(1.f)), ImVec2(c.x, c.y - S(4.f)), ac, S(1.6f));
                dl->AddLine(ImVec2(c.x + S(3.5f), c.y - S(1.f)), ImVec2(c.x, c.y - S(4.f)), ac, S(1.6f));
            } else {
                dl->AddLine(ImVec2(c.x, c.y - S(4.f)), ImVec2(c.x, c.y + S(4.f)), ac, S(1.6f));
                dl->AddLine(ImVec2(c.x - S(3.5f), c.y + S(1.f)), ImVec2(c.x, c.y + S(4.f)), ac, S(1.6f));
                dl->AddLine(ImVec2(c.x + S(3.5f), c.y + S(1.f)), ImVec2(c.x, c.y + S(4.f)), ac, S(1.6f));
            }
            return pressed && enabled && interactive;
        };

        if (draw_arrow_btn("##up", bx, true, can_up)) {
            req_swap_i = i;
            req_dir = -1;
        }
        if (draw_arrow_btn("##dn", bx + btn_s + gap, false, can_dn)) {
            req_swap_i = i;
            req_dir = +1;
        }

        // 正常推进到下一行
        ImGui::SetCursorScreenPos(ImVec2(slot0.x, slot0.y + step));
        ImGui::Dummy(ImVec2(0.01f, 0.01f));
        ImGui::PopID();
    }

    if (req_swap_i >= 0 && req_dir != 0 && !animating) {
        int j = req_swap_i + req_dir;
        if (j >= 0 && j < n_fields) {
            std::swap(b.order[req_swap_i], b.order[j]);
            // 被点行现在在 j,从旧位 i 滑入
            st->SetInt(k_anim_a, j);
            st->SetInt(k_anim_b, req_swap_i);
            st->SetInt(k_anim_dir, req_dir);
            st->SetFloat(k_anim_t, 0.f);
        }
    }

    // 预览 —— 每帧按当前 builder 重算,和真实 chatbox 同路径
    ImGui::Dummy(ImVec2(0, S(6.f)));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Preview", "预览", "預覽"));
    ImGui::PopFont();
    {
        // 重要:传入的 b 是正在编辑的那份;先写回对应字段再渲染,
        // 保证预览和真实 chatbox 用同一份 builder。
        State preview = preview_s;
        if (for_lyrics) preview.fmt_with_lyrics = b;
        else            preview.fmt_without_lyrics = b;

        // 有歌词模板:同时给"播放中"和"暂停"两份预览,避免用户误以为暂停会丢词。
        if (for_lyrics) {
            preview.np_playing = true;
            std::string prev_play = PreviewBuilder(preview, true);
            preview.np_playing = false;
            std::string prev_pause = PreviewBuilder(preview, true);
            if (prev_play.empty()) prev_play = i18n::t("(empty)", "(空)", "(空)");
            if (prev_pause.empty()) prev_pause = i18n::t("(empty)", "(空)", "(空)");

            auto draw_prev = [&](const char* tag, const std::string& text) {
                ImGui::PushFont(font_caption);
                ImGui::TextColored(col::text_dim, "%s", tag);
                ImGui::PopFont();
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                float w = ContentW();
                ImVec2 tsz = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - S(16.f));
                float h = ImMax(S(40.f), tsz.y + S(16.f));
                dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), U32(col::bg_input), S(6.f));
                dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), U32(col::stroke, 0.7f), S(6.f));
                ImFont* f = font_body ? font_body : ImGui::GetFont();
                dl->AddText(f, ImGui::GetFontSize(),
                            ImVec2(p0.x + S(8.f), p0.y + S(8.f)),
                            U32(col::text), text.c_str(), nullptr, w - S(16.f));
                ImGui::Dummy(ImVec2(w, h));
            };
            draw_prev(i18n::t("Playing", "播放中", "播放中"), prev_play);
            ImGui::Dummy(ImVec2(0, S(4.f)));
            draw_prev(i18n::t("Paused", "暂停时", "暫停時"), prev_pause);
        } else {
            std::string prev = PreviewBuilder(preview, false);
            if (prev.empty()) prev = i18n::t("(empty)", "(空)", "(空)");
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ContentW();
            ImVec2 tsz = ImGui::CalcTextSize(prev.c_str(), nullptr, false, w - S(16.f));
            float h = ImMax(S(40.f), tsz.y + S(16.f));
            dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), U32(col::bg_input), S(6.f));
            dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), U32(col::stroke, 0.7f), S(6.f));
            ImFont* f = font_body ? font_body : ImGui::GetFont();
            dl->AddText(f, ImGui::GetFontSize(),
                        ImVec2(p0.x + S(8.f), p0.y + S(8.f)),
                        U32(col::text), prev.c_str(), nullptr, w - S(16.f));
            ImGui::Dummy(ImVec2(w, h));
        }
    }

    ImGui::Dummy(ImVec2(0, S(4.f)));
    if (NLButton(i18n::t("Reset to default##fmt", "恢复默认##fmt", "恢復預設##fmt"),
                 ContentW(), S(28.f), /*accent*/false)) {
        b = for_lyrics ? DefaultFmtWithLyrics() : DefaultFmtNoLyrics();
        st->SetFloat(k_anim_t, 1.f);
        st->SetInt(k_anim_a, -1);
        st->SetInt(k_anim_b, -1);
        st->SetInt(k_anim_dir, 0);
    }

    (void)list0;
    ImGui::PopID();
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
        // Crescent via two arcs — no filled circle that lights up on blur titlebar.
        float r = size * 0.40f;
        const int segs = 20;
        for (int i = 0; i < segs; ++i) {
            float a0 = -1.35f + (2.70f) * (float)i / (float)segs;
            float a1 = -1.35f + (2.70f) * (float)(i + 1) / (float)segs;
            ImVec2 p0(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r);
            ImVec2 p1(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r);
            dl->AddLine(p0, p1, col, S(1.7f));
        }
        // inner cut arc (offset) to suggest crescent thickness
        ImVec2 c2(c.x + r * 0.38f, c.y - r * 0.10f);
        float r2 = r * 0.72f;
        for (int i = 0; i < segs; ++i) {
            float a0 = -1.10f + (2.20f) * (float)i / (float)segs;
            float a1 = -1.10f + (2.20f) * (float)(i + 1) / (float)segs;
            ImVec2 p0(c2.x + std::cos(a0) * r2, c2.y + std::sin(a0) * r2);
            ImVec2 p1(c2.x + std::cos(a1) * r2, c2.y + std::sin(a1) * r2);
            dl->AddLine(p0, p1, col, S(1.4f));
        }
    }
    // Droplet / frost mark — outline only, no filled bright core.
    static void DrawBlur(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
        float r = size * 0.34f;
        // soft outer ring
        dl->AddCircle(c, r, col, 24, S(1.5f));
        // droplet tip pointing up
        ImVec2 tip(c.x, c.y - r * 1.15f);
        ImVec2 bl(c.x - r * 0.55f, c.y + r * 0.15f);
        ImVec2 br(c.x + r * 0.55f, c.y + r * 0.15f);
        dl->AddLine(tip, bl, col, S(1.5f));
        dl->AddLine(tip, br, col, S(1.5f));
        dl->AddLine(bl, br, col, S(1.3f));
    }
}

// ContentW 前向声明见上方 DrawFmtBuilderEditor 之前。

bool NLToggle(const char* label, bool* v) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGuiID id = win->GetID(label);
    const float row_h  = S(22.f);
    const float pill_w = S(28.f), pill_h = S(14.f);
    const float content_w = ContentW();

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
    const float content_w = ContentW();

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
    float w = ContentW();
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
    if (width == 0.f) width = ContentW();
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

    if (width == 0.f) width = ContentW();
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
    if (width == 0.f) width = ContentW();
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
//   - 整体 160ms 淡入/轻移,选项不再逐条飞入。
bool NLCombo(const char* id, int* current, const char* const* items, int count, float width) {
    if (!current || !items || count <= 0) return false;
    if (*current < 0 || *current >= count) *current = 0;

    if (width <= 0.f) width = ContentW();
    const float height   = S(32.f);
    const float rounding = S(6.f);
    const float item_h   = S(30.f);
    const float pad_y    = S(6.f);
    const float gap      = S(2.f);
    const float desired_h=pad_y*2.f+item_h*(float)count+gap*(float)ImMax(0,count-1);

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
    ImGuiID k_reveal   = win->GetID("##reveal_selected");

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
            // Keep the current visual position when reversing a closing panel.
            st->SetBool(k_pill0, false);
            st->SetBool(k_reveal,true);
            // 本帧鼠标还按着,别立刻被"点外"逻辑关掉
            st->SetBool(k_ignore, true);
        }
    }

    if (want_open && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        want_open=false;
        st->SetBool(k_want,false);
    }
    float open_vis=AnimateValue(k_vis,want_open ? 1.f : 0.f,.16f);

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

    const float ease  = open_vis;
    const float slide = (1.f - ease) * S(4.f);
    const float alpha = ease;
    const ImVec2 viewport=ImGui::GetIO().DisplaySize;
    const float margin=S(8.f);
    const float below=ImMax(0.f,viewport.y-margin-bb.Max.y-S(4.f));
    const float above=ImMax(0.f,bb.Min.y-margin-S(4.f));
    const float max_h=ImMin(desired_h,S(266.f));
    const bool upwards=below<max_h && above>below;
    const float popup_h=ImMax(1.f,ImMin(max_h,upwards?above:below));
    const float panel_w=ImMax(1.f,ImMin(width,viewport.x-margin*2.f));
    const float panel_x=ImClamp(bb.Min.x,margin,ImMax(margin,viewport.x-margin-panel_w));
    const float settled_y=upwards?bb.Min.y-S(4.f)-popup_h:bb.Max.y+S(4.f);
    const ImVec2 panel_pos(panel_x,ImClamp(settled_y+(upwards?slide:-slide),margin,ImMax(margin,viewport.y-margin-popup_h)));
    const ImVec2 panel_sz(panel_w,popup_h);
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
        if (!want_open) cflags |= ImGuiWindowFlags_NoInputs;
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
        // Respect the theme material alpha. Previously this forced glass to 1.
        ImGui::SetNextWindowBgAlpha(ImClamp(alpha*col::bg_popup.w,0.f,1.f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, col::bg_popup);
        ImGui::PushStyleColor(ImGuiCol_Border,   col::popup_border);
        ImGui::PushStyleColor(ImGuiCol_Text,     col::text);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   S(8.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(S(4.f), pad_y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(0.f, gap));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,            ImClamp(alpha, 0.f, 1.f));

        ImGuiWindowFlags pflags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin(panel_name, nullptr, pflags)) {
            // 保证盖在 content / catcher 之上
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            if(st->GetBool(k_reveal,false)){
                ImGui::SetScrollY(ImMax(0.f,(*current)*(item_h+gap)-(popup_h-pad_y*2.f-item_h)*.5f));
                st->SetBool(k_reveal,false);
            }

            ImDrawList* pdl = ImGui::GetWindowDrawList();
            ImVec2 content0 = ImGui::GetCursorScreenPos();
            float pill_target = (float)(*current) * (item_h + gap) + item_h * 0.5f;

            // Interactive once nearly open; closing does not consume clicks below it.
            const bool interactive = want_open && open_vis > 0.90f;

            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                bool sel = (i == *current);
                const char* label = items[i] ? items[i] : "";

                const float item_dx=0.f;
                const float item_a=alpha;

                ImVec2 row_min = ImGui::GetCursorScreenPos();
                ImVec2 row_sz(ImMax(1.f,ImGui::GetContentRegionAvail().x),item_h);
                if (interactive) ImGui::InvisibleButton("##row", row_sz);
                else             ImGui::Dummy(row_sz);

                bool row_hov = interactive && ImGui::IsItemHovered();
                bool row_clk = interactive && ImGui::IsItemClicked();

                ImVec2 row_max(row_min.x + row_sz.x, row_min.y + row_sz.y);
                if (sel) pill_target = (row_min.y + row_max.y) * 0.5f - content0.y;

                if ((sel || row_hov) && item_a > 0.01f) {
                    ImU32 fill = sel ? U32(col::accent, (row_hov ? 0.28f : 0.16f) * item_a)
                                     : U32(col::popup_hover, item_a);
                    pdl->AddRectFilled(ImVec2(row_min.x + item_dx, row_min.y),
                                       row_max, fill, S(5.f));
                }

                ImVec2 lsz = ImGui::CalcTextSize(label);
                pdl->PushClipRect(row_min,row_max,true);
                pdl->AddText(ImVec2(row_min.x + S(12.f) + item_dx,
                                    row_min.y + (item_h - lsz.y) * 0.5f),
                             sel ? U32(col::accent, item_a) : U32(col::text, item_a),
                             label);
                pdl->PopClipRect();

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

// NL Button — soft color response and a small press inset, no expanding glow.
// accent=true 用主色;danger=true 优先用红色;否则用 input 配色。
// disabled=true 灰一档且点击无效。
bool NLButton(const char* label, float width, float height,
              bool accent, bool danger, bool disabled) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    if (width == 0.f)  width  = ContentW();
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

    ImGuiStorage* storage=ImGui::GetStateStorage();
    auto key=[&](const char* suffix){return ImHashStr(suffix,0,id);};
    motion::SoftPress spring;
    spring.scale=storage->GetFloat(key("scale"),1.f);
    spring.from=storage->GetFloat(key("from"),1.f);
    spring.elapsed=storage->GetFloat(key("elapsed"),.32f);
    spring.held_time=storage->GetFloat(key("hold-time"),0.f);
    spring.held=storage->GetBool(key("held"),false);
    spring.quick=storage->GetBool(key("quick"),false);
    const float scale=spring.Tick(held,ImGui::GetIO().DeltaTime,interface_motion_enabled&&!disabled);
    storage->SetFloat(key("scale"),spring.scale);storage->SetFloat(key("from"),spring.from);
    storage->SetFloat(key("elapsed"),spring.elapsed);storage->SetFloat(key("hold-time"),spring.held_time);
    storage->SetBool(key("held"),spring.held);storage->SetBool(key("quick"),spring.quick);
    const int first_vertex=win->DrawList->VtxBuffer.Size;
    ImVec2 bmin=bb.Min,bmax=bb.Max;

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
        hover_bg = ImVec4(Lerp(col::accent.x,1.f,.08f),Lerp(col::accent.y,1.f,.08f),Lerp(col::accent.z,1.f,.08f),1.f);
        text_col = ImVec4(0.05f, 0.07f, 0.10f, 1.f);
    } else {
        base_bg  = col::bg_input;
        hover_bg = col::bg_hover;
        text_col = col::text;
    }
    ImU32 bg_u32 = Mix(base_bg, hover_bg, t_hov);
    ImVec4 pressed_bg=ImGui::ColorConvertU32ToFloat4(bg_u32);
    pressed_bg.x*=1.f-.12f*t_press;pressed_bg.y*=1.f-.12f*t_press;pressed_bg.z*=1.f-.12f*t_press;
    bg_u32=U32(pressed_bg);


    win->DrawList->AddRectFilled(bmin, bmax, bg_u32, S(6.f));
    if(t_press>.001f)win->DrawList->AddRect(bmin,bmax,U32(ImVec4(.04f,.12f,.18f,.16f*t_press)),S(6.f),0,S(1.f));

    // hover 边亮一圈(非 accent 才显)
    if (!accent && !danger && !disabled && t_hov > 0.01f) {
        ImVec4 stroke_c = col::stroke; stroke_c.w *= t_hov * .8f;
        win->DrawList->AddRect(bmin, bmax, ImGui::ColorConvertFloat4ToU32(stroke_c),
                               S(6.f), 0, S(1.f));
    }

    const char* label_end = RenderedTextEnd(label);
    ImVec2 lsz = ImGui::CalcTextSize(label, label_end);
    ImVec2 lpos((bmin.x + bmax.x - lsz.x) * 0.5f, (bmin.y + bmax.y - lsz.y) * 0.5f);
    win->DrawList->AddText(nullptr, 0.f, lpos, U32(text_col), label, label_end);
    const ImVec2 center=bb.GetCenter();
    for(int n=first_vertex;n<win->DrawList->VtxBuffer.Size;++n){
        ImVec2& p=win->DrawList->VtxBuffer[n].pos;
        p.x=center.x+(p.x-center.x)*scale;p.y=center.y+(p.y-center.y)*scale;
    }
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
// 卡片内左右边距。左靠 Indent,右靠控件宽度扣掉 g_card_side_pad,
// 避免输入框/开关顶到卡片右缘。
static float  g_card_side_pad = 0.f;

// 卡片内外统一的可用宽度:左 Indent 已经吃掉左边距,这里再扣右边距。
static float ContentW(float extra_shrink) {
    float w = ImGui::GetContentRegionAvail().x - g_card_side_pad - extra_shrink;
    return ImMax(1.f, w);
}

static void CardBegin(const char* /*id*/) {

    g_card_start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);  // content channel
    const float pad = S(14.f);
    ImGui::Indent(pad);
    g_card_side_pad = pad;      // 对称右边距,由 ContentW() 扣除
    ImGui::Dummy(ImVec2(0, S(8.f)));
}

static void CardEnd() {
    ImGui::Dummy(ImVec2(0, S(4.f)));
    ImGui::Unindent(S(14.f));
    g_card_side_pad = 0.f;

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


    ImGui::Dummy(ImVec2(0, S(10.f)));
}

// 状态点(● / ○ 的替代)。
// Active is solid, inactive is outlined. No perpetual pulsing.
// 调用方需要自己安排好 cursor 位置 —— 此函数只画,不动 cursor。
// 返回值是 dot 的右边沿 x,用来后接 label 文本。
static float DrawStatusDot(ImVec2 origin, float radius, bool active, ImU32 col_on, ImU32 col_off) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(origin.x + radius, origin.y + radius);
    if (active) {
        dl->AddCircleFilled(c, radius, col_on, 16);
    } else {
        dl->AddCircle(c, radius, col_off, 16, S(1.4f));
    }
    return c.x + radius;
}

// Render a stable status label and dot.
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
    if (!interface_motion_enabled) { st->SetFloat(storage_key,target); return target; }
    float dt  = ImGui::GetIO().DeltaTime;
    if (dt > 0.05f) dt = 0.05f;
    // 跟随系数 10/sec,大跳的时候 ~300ms 跟上
    cur += (target - cur) * (1.f - std::exp(-10.f * dt));
    if (std::fabs(target - cur) < 1.f / 2048.f) cur = target;
    st->SetFloat(storage_key, cur);
    return cur;
}

// RGB fringe and sparse randomized glyph tears, with a clean recovery phase.
static void DrawGlitchWordmark(ImDrawList* dl,ImVec2 pos,const char* text,
                               const motion::GlitchFrame& effect) {
    if(effect.strength<=0){dl->AddText(pos,U32(col::text),text);return;}
    const ImVec2 size=ImGui::CalcTextSize(text);
    const float a=effect.strength;
    const ImVec4 rgb[]={ImVec4(1.f,.12f,.08f,1.f),ImVec4(.28f,1.f,.08f,1.f),ImVec4(.20f,.12f,1.f,1.f)};
    const ImVec2 offsets[]={ImVec2(-S(.5f),-S(1.5f)),ImVec2(-S(1.7f),S(1.1f)),ImVec2(S(1.9f),S(.35f))};
    dl->PushClipRect(ImVec2(S(8.f),0),ImVec2(S(182.f),S(55.f)),true);
    // Soft halos are local to the glyphs, not a glowing rectangle or flash.
    for(int channel=0;channel<3;++channel){
        const ImVec2 origin(pos.x+(offsets[channel].x+S(effect.jitter))*a,pos.y+offsets[channel].y*a);
        for(int n=0;n<8;++n){
            const float angle=n*(IM_PI/4.f),radius=S(2.f)*a;
            dl->AddText(ImVec2(origin.x+std::cos(angle)*radius,origin.y+std::sin(angle)*radius),
                U32(rgb[channel],a*.045f),text);
        }
        dl->AddText(origin,U32(rgb[channel],a*.78f),text);
    }
    auto core=[&](ImVec2 min,ImVec2 max,float displacement){
        if(max.x<=min.x || max.y<=min.y)return;
        dl->PushClipRect(min,max,true);
        if(displacement!=0){
            for(int channel=0;channel<3;++channel)
                dl->AddText(ImVec2(pos.x+displacement+offsets[channel].x*a,pos.y+offsets[channel].y*a),U32(rgb[channel],a*.65f),text);
        }
        dl->AddText(ImVec2(pos.x+displacement,pos.y),U32(col::text),text);
        dl->PopClipRect();
    };
    float previous_y=pos.y;
    for(int i=0;i<effect.count;++i){
        const auto& tear=effect.tears[i];
        const float y=pos.y+tear.y*size.y;
        const float bottom=y+ImMax(S(.65f),tear.height*size.y);
        const float x=pos.x+tear.x*size.x,end=x+tear.width*size.x;
        core(ImVec2(pos.x-S(5.f),previous_y),ImVec2(pos.x+size.x+S(5.f),y),0);
        core(ImVec2(pos.x-S(5.f),y),ImVec2(x,bottom),0);
        core(ImVec2(x,y),ImVec2(end,bottom),tear.shift*size.y*a);
        core(ImVec2(end,y),ImVec2(pos.x+size.x+S(5.f),bottom),0);
        previous_y=bottom;
    }
    core(ImVec2(pos.x-S(5.f),previous_y),ImVec2(pos.x+size.x+S(5.f),pos.y+size.y+S(2.f)),0);
    dl->PopClipRect();
}

static void DrawTitleBarContent(State& s, int win_w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = S(40.f);

    ImGui::PushFont(font_logo);
    const char* logo = "VRC LYRICS";
    ImVec2 sz = ImGui::CalcTextSize(logo);
    static const uint32_t glitch_seed=GetTickCount()^0x71ac31u;
    const auto glitch=motion::TitleGlitch(ImGui::GetTime(),glitch_seed,s.ui_motion);
    // The visible sidebar header includes its 14px top padding below the
    // 41px title strip. Center against all 55px, not the window-button strip.
    // Foreground draw avoids clipping the lowered glow at the titlebar edge.
    const float logo_y=(S(55.f)-sz.y)*.5f+S(2.f);
    DrawGlitchWordmark(ImGui::GetForegroundDrawList(),ImVec2((S(190.f)-sz.x)*.5f,logo_y),logo,glitch);
    ImGui::PopFont();

    if (s.save_toast_sec > 0.f) {
        s.save_toast_sec -= ImGui::GetIO().DeltaTime;
        float alpha = ImMin(1.f, s.save_toast_sec * 2.f);
        ImGui::PushFont(font_body);
        const char* msg = i18n::t("Saved", "已保存", "已儲存");
        ImVec2 msz = ImGui::CalcTextSize(msg);
        ImVec2 mp(S(214.f), (h - msz.y) * 0.5f);
        ImVec4 pill_col = col::accent; pill_col.w = alpha * 0.85f;
        dl->AddRectFilled(ImVec2(mp.x - S(8.f), mp.y - S(3.f)),
                          ImVec2(mp.x + msz.x + S(8.f), mp.y + msz.y + S(3.f)),
                          ImGui::ColorConvertFloat4ToU32(pill_col), S(8.f));
        dl->AddText(mp, U32(ImVec4(0.05f, 0.07f, 0.10f, alpha)), msg);
        ImGui::PopFont();
    }

    // 主题按钮图标 = 当前主题的"下一档"提示:
    //   Dark  → sun  (切到 Light)
    //   Light → blur (切到 Blur)
    //   Blur  → moon (切回 Dark)
    auto theme_icon = [](Theme t) {
        if (t == Theme::Dark)  return icons::DrawSun;
        if (t == Theme::Light) return icons::DrawBlur;
        return icons::DrawMoon;
    };
    struct IconBtn { const char* id; void(*draw)(ImDrawList*, ImVec2, float, ImU32); };
    IconBtn fns[] = {
        { "##i_save",  icons::DrawSave },
        { "##i_theme", theme_icon(s.theme) },
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
    const float close_hover=Anim(ImGui::GetID("##close_hover"),hov_close,20.f);
    if (close_hover>.001f)
        dl->AddRectFilled(ImVec2(close_x, (h - btn_size) * 0.5f),
                          ImVec2(close_x + btn_size, (h + btn_size) * 0.5f),
                          U32(ImVec4(0.85f, 0.25f, 0.30f, .9f*close_hover)), S(4.f));
    icons::DrawClose(dl, ImVec2(close_x + btn_size * 0.5f, h * 0.5f), S(16.f),
                     U32(hov_close ? col::text : col::text_dim));

    ImGui::SetCursorScreenPos(ImVec2(min_x, (h - btn_size) * 0.5f));
    bool clicked_min = ImGui::InvisibleButton("##min", ImVec2(btn_size, btn_size));
    bool hov_min = ImGui::IsItemHovered();
    const float min_hover=Anim(ImGui::GetID("##min_hover"),hov_min,20.f);
    if (min_hover>.001f)
        dl->AddRectFilled(ImVec2(min_x, (h - btn_size) * 0.5f),
                          ImVec2(min_x + btn_size, (h + btn_size) * 0.5f),
                          U32(col::bg_hover,min_hover), S(4.f));
    icons::DrawMinimize(dl, ImVec2(min_x + btn_size * 0.5f, h * 0.5f), S(16.f),
                        U32(hov_min ? col::text : col::text_dim));

    float ix = fn_right - btn_size;
    for (int i = (int)IM_ARRAYSIZE(fns) - 1; i >= 0; --i, ix -= btn_size + gap) {
        ImGui::SetCursorScreenPos(ImVec2(ix, (h - btn_size) * 0.5f));
        bool fn_click = ImGui::InvisibleButton(fns[i].id, ImVec2(btn_size, btn_size));
        bool hov = ImGui::IsItemHovered();
        const float toolbar_hover=Anim(ImHashStr("hover",0,ImGui::GetID(fns[i].id)),hov,20.f);
        if (toolbar_hover>.001f)
            dl->AddRectFilled(ImVec2(ix, (h - btn_size) * 0.5f),
                              ImVec2(ix + btn_size, (h + btn_size) * 0.5f),
                              U32(col::bg_hover,toolbar_hover), S(4.f));
        fns[i].draw(dl, ImVec2(ix + btn_size * 0.5f, h * 0.5f), S(14.f),
                    U32(hov ? col::text : col::text_dim));
        if (fn_click && std::strcmp(fns[i].id, "##i_save") == 0) {
            s.save_request = true;
        }
        if (fn_click && std::strcmp(fns[i].id, "##i_theme") == 0) {
            Theme prev = s.theme;
            // Dark → Light → Blur → Dark
            if (s.theme == Theme::Dark)       s.theme = Theme::Light;
            else if (s.theme == Theme::Light) s.theme = Theme::Blur;
            else                              s.theme = Theme::Dark;
            BeginThemeTransition(prev, s.theme, s.blur_opacity);
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
    if (t_hov > 0.01f) {
        win->DrawList->AddRectFilled(bb.Min, bb.Max,
            U32(col::bg_hover, t_hov * (lightish ? 0.95f : 0.55f)), 0.f);
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

    g_sidebar_indicator_y = AnimateValue(ImGui::GetID("##navigation-position"),target_y,.24f);

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    float h = S(34.f);
    win->DrawList->AddRectFilled(
        ImVec2(win->Pos.x, g_sidebar_indicator_y + S(8.f)),
        ImVec2(win->Pos.x + S(3.f), g_sidebar_indicator_y + h - S(8.f)),
        U32(col::accent), S(1.5f));
}


// Focused lyrics screen. The live Chatbox preview uses the sender's formatter.
static void DrawLyrics(State& s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float available_h = ImGui::GetContentRegionAvail().y;
    const bool compact = width < S(590.f);
    const float header_h = S(90.f);
    float stage_h = ImClamp(available_h - S(compact ? 440.f : 325.f), S(160.f), S(380.f));
    auto at = [&](float x, float y) { ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y + y)); };
    auto text = [&](ImFont* font, float size, ImVec2 p, const ImVec4& color,
                    const std::string& value, float wrap = 0.f, float alpha = 1.f) {
        dl->AddText(font, size, p, U32(color, alpha * ImGui::GetStyle().Alpha),
                    value.c_str(), nullptr, wrap);
    };
    const float cover = S(76.f);
    if (s.np_detected && s.cover_square_srv) {
        dl->AddImageRounded((ImTextureID)s.cover_square_srv, origin,
            ImVec2(origin.x + cover, origin.y + cover), ImVec2(0,0), ImVec2(1,1),
            IM_COL32_WHITE, S(8.f));
    } else {
        // Reuse the established app icon when no artwork is supplied.
        dl->AddRectFilled(origin, ImVec2(origin.x + cover, origin.y + cover), U32(col::bg_card), S(8.f));
        icons::DrawMusic(dl, ImVec2(origin.x + cover*.5f, origin.y + cover*.5f), S(26.f), U32(col::accent));
    }
    const float meta_x = origin.x + cover + S(20.f);
    dl->PushClipRect(ImVec2(meta_x, origin.y), ImVec2(origin.x + width, origin.y + header_h), true);
    text(font_title, S(22.f), ImVec2(meta_x, origin.y + S(4.f)), col::text,
        s.np_detected ? s.np_title : i18n::t("No music detected", "未检测到音乐", "未偵測到音樂"));
    std::string artist = s.np_artist;
    if (s.np_album[0]) artist += std::string(" · ") + s.np_album;
    text(font_body, S(14.f), ImVec2(meta_x, origin.y + S(34.f)), col::text_dim,
        s.np_detected ? artist : i18n::t("Open your music player to get started", "打开音乐播放器即可开始", "開啟音樂播放器即可開始"));
    const char* source = s.np_source == 1 ? i18n::t("NetEase Cloud", "网易云音乐", "網易雲音樂") :
        s.np_source == 2 ? "Spotify" : s.np_source == 3 ? "YouTube Music" : "SMTC";
    std::string meta = s.np_detected ? std::string(source) + " · " +
        (s.np_playing ? i18n::t("Playing", "播放中", "播放中") : i18n::t("Paused", "已暂停", "已暫停")) : "";
    if (s.np_detected && s.np_ncm_id[0] && !compact)
        meta += std::string("   ·   ") + i18n::t("Track ID: ", "曲目 ID: ", "曲目 ID: ") + s.np_ncm_id;
    text(font_caption, S(12.f), ImVec2(meta_x, origin.y + S(58.f)), col::text_dim, meta);
    dl->PopClipRect();

    std::string lyric = s.np_current_line;
    if (lyric.empty()) lyric = !s.np_detected ? i18n::t("Waiting for music", "等待音乐播放", "等待音樂播放") :
        s.np_has_lyrics ? i18n::t("Instrumental", "静听此刻", "靜聽此刻") :
        i18n::t("Lyrics unavailable", "暂无歌词", "暫無歌詞");
    // Long lines expand the document rather than being cut by the animation clip.
    const size_t break_at = lyric.find('\n');
    const std::string lead = lyric.substr(0, break_at);
    const std::string translated = break_at == std::string::npos ? "" : lyric.substr(break_at+1);
    const float wrap = ImMax(S(80.f), width-S(36.f));
    float measured_size = S(compact ? 30.f : 56.f);
    const float full_width = font_lyrics->CalcTextSizeA(measured_size,FLT_MAX,0,lead.c_str()).x;
    if (full_width > wrap) measured_size = ImMax(S(23.f), measured_size*wrap/full_width);
    const float text_h = font_lyrics->CalcTextSizeA(measured_size,FLT_MAX,wrap,lead.c_str()).y;
    const float translation_h = translated.empty() ? 0.f : S(15.f)+
        font_body->CalcTextSizeA(S(compact ? 20.f : 27.f),FLT_MAX,wrap,translated.c_str()).y;
    stage_h = ImMax(stage_h, text_h+translation_h+S(70.f));
    static LyricMotion motion;
    motion.Update(s.np_track_key, lyric, s.np_pos_ms, ImGui::GetIO().DeltaTime, s.lyric_motion);
    const float stage_y = origin.y + header_h;
    const char* caption = i18n::t("CURRENT LYRIC", "当前歌词", "目前歌詞");
    const ImVec2 cap_size = font_body->CalcTextSizeA(S(13.f), FLT_MAX, 0.f, caption);
    text(font_body, S(13.f), ImVec2(origin.x + (width-cap_size.x)*.5f, stage_y + S(10.f)), col::text_dim, caption);
    dl->PushClipRect(ImVec2(origin.x, stage_y + S(34.f)),
        ImVec2(origin.x + width, stage_y + stage_h - S(8.f)), true);
    auto draw_line = [&](const std::string& value, float offset, float scale, float alpha) {
        if (value.empty() || alpha <= .001f) return;
        const size_t split = value.find('\n');
        const std::string main = value.substr(0, split);
        const std::string translation = split == std::string::npos ? "" : value.substr(split + 1);
        const float max_w = ImMax(S(80.f), width - S(36.f));
        float size = S(compact ? 30.f : 56.f);
        const float unwrapped = font_lyrics->CalcTextSizeA(size, FLT_MAX, 0.f, main.c_str()).x;
        if (unwrapped > max_w) size = ImMax(S(23.f), size * max_w / unwrapped);
        size *= scale;
        const ImVec2 measure = font_lyrics->CalcTextSizeA(size, FLT_MAX, max_w, main.c_str());
        const float tr_size = S(compact ? 20.f : 27.f) * scale;
        const ImVec2 tr = font_body->CalcTextSizeA(tr_size, FLT_MAX, max_w, translation.c_str());
        const float total_h = measure.y + (translation.empty() ? 0.f : S(15.f) + tr.y);
        float y = stage_y + S(34.f) + (stage_h - S(42.f) - total_h)*.5f + offset;
        alpha *= ImClamp((y-stage_y-S(34.f))/S(18.f),0.f,1.f);
        alpha *= ImClamp((stage_y+stage_h-S(8.f)-y-total_h)/S(18.f),0.f,1.f);
        // Center each wrapped line independently, including CJK and long metadata.
        auto centered = [&](ImFont* f, float px, const std::string& str, const ImVec4& color) {
            const char* p = str.c_str(); const char* end = p + str.size();
            while (p < end) {
                const char* line_end = f->CalcWordWrapPosition(px, p, end, max_w);
                if (line_end <= p) { line_end = p + 1; while (line_end < end && ((unsigned char)*line_end & 0xc0) == 0x80) ++line_end; }
                const char* newline = (const char*)memchr(p, '\n', (size_t)(line_end-p));
                if (newline) line_end = newline;
                std::string row(p, line_end);
                const float rw = f->CalcTextSizeA(px, FLT_MAX, 0.f, row.c_str()).x;
                text(f, px, ImVec2(origin.x + (width-rw)*.5f, y), color, row, 0, alpha);
                y += px;
                p = line_end;
                if (p < end && *p == '\n') ++p;
                while (p < end && *p == ' ') ++p;
            }
        };
        centered(font_lyrics, size, main, col::accent);
        if (!translation.empty()) { y += S(15.f); centered(font_body, tr_size, translation, col::text); }
    };
    const float phase = motion.Progress(), eased = motion.Ease();
    const float travel = ImMax(S(145.f), stage_h*.64f);
    const float old_alpha = ImMax(0.f, 1.f-phase*2.5f);
    draw_line(motion.outgoing, -travel*eased, 1.f - .035f*eased, old_alpha*old_alpha*.65f);
    draw_line(motion.current, travel*(1.f-eased), .95f + .05f*eased, ImMin(1.f, phase*3.f));
    dl->PopClipRect();

    const float progress_y = header_h + stage_h + S(6.f);
    char elapsed[24], duration[24];
    const int pos = ImMax(0, s.np_pos_ms/1000), dur = ImMax(0, s.np_dur_ms/1000);
    snprintf(elapsed, sizeof elapsed, "%02d:%02d", pos/60, pos%60);
    snprintf(duration, sizeof duration, "%02d:%02d", dur/60, dur%60);
    text(font_body, S(13.f), ImVec2(origin.x, origin.y+progress_y-S(4.f)), col::text_dim, elapsed);
    const float dw = font_body->CalcTextSizeA(S(13.f), FLT_MAX, 0, duration).x;
    text(font_body, S(13.f), ImVec2(origin.x+width-dw, origin.y+progress_y-S(4.f)), col::text_dim, duration);
    ImVec2 bar0(origin.x + S(62.f), origin.y+progress_y+S(3.f));
    const float bar_w = ImMax(1.f, width - S(124.f));
    dl->AddRectFilled(bar0, ImVec2(bar0.x+bar_w, bar0.y+S(4.f)), U32(col::stroke), S(2.f));
    const float progress = s.np_dur_ms > 0 ? ImClamp((float)s.np_pos_ms/s.np_dur_ms, 0.f, 1.f) : 0.f;
    if (progress > 0) dl->AddRectFilled(bar0, ImVec2(bar0.x+bar_w*progress, bar0.y+S(4.f)), U32(col::accent), S(2.f));

    const float lower_y = progress_y + S(54.f);
    const float controls_w = S(210.f);
    const float preview_w = compact ? width : width-controls_w-S(34.f);
    text(font_medium, S(17.f), ImVec2(origin.x, origin.y+lower_y), col::text, "Chatbox " + std::string(i18n::t("preview", "预览", "預覽")));
    const std::string prefix = EffectiveStatusPrefix(s);
    const std::string preview = RenderChatbox(s, prefix.c_str(), s.np_playing,
        s.np_has_lyrics && s.np_current_line[0], s.np_current_line);
    const float preview_h = ImMax(S(132.f), font_body->CalcTextSizeA(S(14.f), FLT_MAX,
        ImMax(S(50.f), preview_w-S(36.f)), preview.c_str()).y + S(64.f));
    const ImVec2 preview0(origin.x, origin.y+lower_y+S(30.f));
    ImVec4 preview_bg = col::bg_card;
    if (s.theme == Theme::Blur) preview_bg = ImVec4(.10f,.11f,.14f,.80f);
    dl->AddRectFilled(preview0, ImVec2(preview0.x+preview_w, preview0.y+preview_h), U32(preview_bg), S(10.f));
    dl->PushClipRect(preview0, ImVec2(preview0.x+preview_w, preview0.y+preview_h-S(32.f)), true);
    text(font_body, S(14.f), ImVec2(preview0.x+S(18.f),preview0.y+S(18.f)), col::text,
        s.np_detected ? preview : i18n::t("Your current track and lyrics will appear here", "识别到歌曲后在这里预览发送内容", "識別到歌曲後在這裡預覽傳送內容"), preview_w-S(36.f));
    dl->PopClipRect();
    const char* link = i18n::t("Adjust format", "调整格式", "調整格式");
    const float link_w = font_body->CalcTextSizeA(S(14.f),FLT_MAX,0,link).x + S(24.f);
    at(preview_w-link_w-S(12.f), lower_y+S(30.f)+preview_h-S(32.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::bg_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::bg_input);
    ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
    if (ImGui::Button(link, ImVec2(link_w, S(26.f)))) {
        s.last_tab=s.current_tab; s.current_tab=Tab::Settings;
        s.tab_transition=0.f; s.scroll_to_format=true;
    }
    ImGui::PopStyleColor(4);
    const float control_y = compact ? lower_y+S(30.f)+preview_h+S(18.f) : lower_y+S(30.f);
    at(compact ? 0 : preview_w+S(34.f), control_y);
    if (NLButton(s.service_running ? i18n::t("Stop sending###lyrics_service", "停止发送歌词###lyrics_service", "停止傳送歌詞###lyrics_service") :
        i18n::t("Send lyrics###lyrics_service", "开始发送歌词###lyrics_service", "開始傳送歌詞###lyrics_service"), controls_w, S(46.f), !s.service_running))
        s.service_running = !s.service_running;
    at(compact ? 0 : preview_w+S(34.f), control_y+S(66.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::BeginChild("##send_controls", ImVec2(controls_w,S(42.f)), false, ImGuiWindowFlags_NoScrollbar);
    NLToggle(i18n::t("Send while paused", "暂停时仍发送", "暫停時仍傳送"), &s.send_while_paused);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    at(0, ImMax(lower_y+S(30.f)+preview_h, control_y+S(108.f))+S(8.f));
    ImGui::Dummy(ImVec2(width, S(1.f)));
}

// Four-choice selector with a fixed-size, 220ms sliding selection surface.
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

    // Keep the ID stable when the selected string changes.
    uintptr_t base   = (uintptr_t)slot;
    ImGuiID anim_id  = win->GetID((const void*)base);
    const float pos = AnimateValue(anim_id,(float)selected,.22f);

    const float btn_w = S(36.f);
    const float btn_h = S(26.f);
    const float gap   = S(2.f);
    const float pitch = btn_w + gap;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = win->DrawList;

    const ImVec2 pill_min(origin.x+pos*pitch,origin.y);
    const ImVec2 pill_max(pill_min.x+btn_w,pill_min.y+btn_h);
    dl->AddRectFilled(pill_min,pill_max,U32(col::accent),S(4.f));

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
    float btn_w = (ContentW() - S(8.f) - S(4.f) * 4) / 5.f;
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

    // 每行:左侧分类名 + 右侧 emoji 选择器,垂直居中对齐。
    // 以前 Text + SameLine(80) 会按字体 baseline 对齐,中文和 emoji 高度不同,看起来漂。
    // 列宽按最长标签量,避免中文短标签和 emoji 之间空一大截。
    const float picker_h = S(26.f); // 跟 AnimatedEmojiPicker 的 btn_h 一致
    float label_col_w = S(48.f);
    ImGui::PushFont(font_body);
    for (auto& row : rows) {
        const char* lab =
            i18n::current == i18n::Lang::SC ? row.label_sc :
            i18n::current == i18n::Lang::TC ? row.label_tc : row.label_en;
        float w = ImGui::CalcTextSize(lab).x;
        if (w + S(10.f) > label_col_w) label_col_w = w + S(10.f);
    }
    ImGui::PopFont();
    if (label_col_w > S(72.f)) label_col_w = S(72.f);

    for (auto& row : rows) {
        const char* lab =
            i18n::current == i18n::Lang::SC ? row.label_sc :
            i18n::current == i18n::Lang::TC ? row.label_tc : row.label_en;

        ImGui::PushFont(font_body);
        ImVec2 lsz = ImGui::CalcTextSize(lab);
        ImGui::PopFont();

        ImVec2 row0 = ImGui::GetCursorScreenPos();
        float row_h = picker_h;
        float text_y = row0.y + (row_h - lsz.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(
            font_body ? font_body : ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(row0.x, text_y),
            U32(col::text_dim), lab);

        // 选择器紧贴标签列,各行 emoji 左缘仍对齐
        ImGui::SetCursorScreenPos(ImVec2(row0.x + label_col_w, row0.y));
        AnimatedEmojiPicker(row.slot, sizeof(s.emoji_game), row.choices);

        // 行距收紧:只留 2px 缝
        ImGui::SetCursorScreenPos(ImVec2(row0.x, row0.y + row_h + S(2.f)));
        ImGui::Dummy(ImVec2(0.01f, 0.01f));
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
            float w = ContentW();
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
            float w = ContentW();
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
        if (NLCombo("##audio_dev", &current_idx, item_ptrs, s.audio_device_count, ContentW(S(80.f)))) {
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
    float btn_w = ContentW();
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
        float w = ContentW();
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

static bool VideoSelect(const char* id,int& index,const std::vector<std::string>& labels) {
    if(labels.empty())return false;
    index=ImClamp(index,0,(int)labels.size()-1);
    std::vector<const char*> items;items.reserve(labels.size());
    for(const auto& label:labels)items.push_back(label.c_str());
    return NLCombo(id,&index,items.data(),(int)items.size(),ContentW());
}
static void DrawVideoTab(State& s) {
    s.video_copy_toast_sec=ImMax(0.f,s.video_copy_toast_sec-ImGui::GetIO().DeltaTime);
    const bool busy=s.video_status==1;
    SectionTitle(i18n::t("BILIBILI VIDEO / LIVE","BILIBILI 视频 / 直播","BILIBILI 影片 / 直播"));
    CardBegin("##video_input");
    ImGui::TextWrapped("%s",i18n::t("BV / AV / video link / b23.tv / live room link",
        "支持 BV / AV 号、视频链接、b23.tv 分享短链、直播间链接。",
        "支援 BV / AV 號、影片連結、b23.tv 分享短連結、直播間連結。"));
    ImGui::BeginDisabled(busy);
    if(NLInputText("##video_in","https://www.bilibili.com/video/...  /  https://live.bilibili.com/...",
        s.video_input,sizeof(s.video_input))){
        s.video_input_changed=true;
        s.video_result={};s.video_result_url[0]=0;s.video_error[0]=0;
        s.video_status=0;s.video_page_index=s.video_quality_index=s.video_stream_index=0;
    }
    if(!s.video_result.pages.empty()){
        ImGui::TextUnformatted(i18n::t("Episode / part","视频分集","影片分集"));
        std::vector<std::string> labels;
        for(const auto& page:s.video_result.pages)labels.push_back("P"+std::to_string(page.number)+" · "+page.title);
        if(VideoSelect("##video_parts",s.video_page_index,labels)){
            s.video_result_url[0]=0;s.video_status=0;
        }
    }
    if(!s.video_result.qualities.empty()){
        ImGui::TextUnformatted(i18n::t("Requested quality","请求清晰度","請求清晰度"));
        std::vector<std::string> labels={i18n::t("Automatic","自动","自動")};
        for(const auto& quality:s.video_result.qualities)labels.push_back(quality.label);
        if(VideoSelect("##video_quality",s.video_quality_index,labels)){
            s.video_result_url[0]=0;s.video_status=0;
        }
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0,S(6.f)));
    const char* action=busy?i18n::t("Parsing...###video_parse","正在解析…###video_parse","正在解析…###video_parse"):
        i18n::t("Parse / refresh###video_parse","解析 / 刷新地址###video_parse","解析 / 更新地址###video_parse");
    if(NLButton(action,ContentW(),S(36.f),true,false,busy||!s.video_input[0])){
        s.video_parse_request=true;
    }
    CardEnd();

    SectionTitle(i18n::t("RESULT","解析结果","解析結果"));
    CardBegin("##video_result");
    if(busy){
        ImGui::TextUnformatted(i18n::t("Retrieving metadata and playable streams…","正在获取视频信息和播放地址…","正在取得影片資訊與播放地址…"));
    }else if(s.video_status==3){
        ImGui::TextColored(ImVec4(1,.55f,.55f,1),"%s",i18n::t("Unable to parse","暂时无法解析","暫時無法解析"));
        ImGui::TextWrapped("%s",s.video_result.message.c_str());
    }else if(s.video_status==2){
        ImGui::PushFont(font_title);ImGui::TextWrapped("%s",s.video_result.title.c_str());ImGui::PopFont();
        ImGui::Text("%s · %s",s.video_result.live?i18n::t("Live","直播","直播"):i18n::t("Video","视频","影片"),
            s.video_result.quality.c_str());
        if(!s.video_result.warning.empty())ImGui::TextWrapped("%s",s.video_result.warning.c_str());
        if(!s.video_result.streams.empty()){
            std::vector<std::string> labels;
            for(const auto& stream:s.video_result.streams)labels.push_back(stream.label+" · "+stream.format);
            if(VideoSelect("##video_streams",s.video_stream_index,labels)){
                const auto& url=s.video_result.streams[s.video_stream_index].url;
                const size_t n=std::min(url.size(),sizeof(s.video_result_url)-1);
                memcpy(s.video_result_url,url.data(),n);s.video_result_url[n]=0;
            }
        }
        NLInputTextMultiline("##video_url",nullptr,s.video_result_url,sizeof(s.video_result_url),ContentW(),S(76.f),ImGuiInputTextFlags_ReadOnly);
        const char* copy=s.video_copy_toast_sec>0?
            i18n::t("Copied###video_copy","已复制###video_copy","已複製###video_copy"):
            i18n::t("Copy playback URL###video_copy","复制播放地址###video_copy","複製播放地址###video_copy");
        if(NLButton(copy,ContentW(),S(34.f),true,false,!s.video_result_url[0])){
            s.video_copy_request=true;s.video_copy_toast_sec=1.4f;
        }
    }else{
        ImGui::TextWrapped("%s",s.video_result.pages.empty()?
            i18n::t("Paste a link to retrieve episodes and quality options.","粘贴链接并解析，即可加载分集和清晰度选项。","貼上連結並解析，即可載入分集和清晰度選項。"):
            i18n::t("Selection changed. Parse again to get a matching URL.","选项已变更，请点击“解析 / 刷新地址”获取对应链接。","選項已變更，請重新解析以取得對應連結。"));
    }
    if(!busy && !s.video_result.source_url.empty()){
        if(NLButton(i18n::t("Copy original page","复制原页面链接","複製原頁面連結"),ContentW(),S(30.f),false))
            s.video_copy_source_request=true;
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

    ImGui::Dummy(ImVec2(0, S(6.f)));
    ImGui::PushFont(font_body);
    ImGui::TextColored(col::text_dim, "%s", i18n::t("Theme", "主题", "主題"));
    ImGui::PopFont();
    // 顺序跟 Theme 枚举一致:0=Dark 1=Light 2=Blur
    const char* theme_en[] = { "Dark", "Light", "Blur (Acrylic)" };
    const char* theme_sc[] = { "深色", "浅色", "毛玻璃" };
    const char* theme_tc[] = { "深色", "淺色", "毛玻璃" };
    const char** theme_labels =
        s.language == i18n::Lang::SC ? theme_sc :
        s.language == i18n::Lang::TC ? theme_tc : theme_en;
    int ti = (int)s.theme;
    if (ti < 0 || ti > 2) ti = 0;
    if (NLCombo("##theme", &ti, theme_labels, 3)) {
        Theme prev = s.theme;
        s.theme = (Theme)ti;
        if (prev != s.theme) BeginThemeTransition(prev, s.theme, s.blur_opacity);
    }
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Blur needs Windows 11 Acrylic. Falls back to dark if unsupported.",
                "毛玻璃需要 Windows 11 Acrylic;不支持时回落深色外观。",
                "毛玻璃需要 Windows 11 Acrylic;不支援時回落深色外觀。"));
    ImGui::PopFont();

    NLToggle(i18n::t("Interface animations", "界面动画", "介面動畫"), &s.ui_motion);

    // 毛玻璃两档:只有选 Blur 时显示,拖动即时生效。
    if (s.theme == Theme::Blur) {
        ImGui::Dummy(ImVec2(0, S(8.f)));
        if (s.blur_opacity < 0) s.blur_opacity = 0;
        if (s.blur_opacity > 100) s.blur_opacity = 100;
        int bo = s.blur_opacity;
        if (NLSliderInt(i18n::t("Blur opacity", "毛玻璃不透明度", "毛玻璃不透明度"),
                        &bo, 0, 100)) {
            s.blur_opacity = bo;
            RefreshBlurOpacity(s.blur_opacity);
        }
        ImGui::PushFont(font_caption);
        ImGui::TextColored(col::text_dim, "%s",
            i18n::t("0 = more glass / desktop, 100 = more solid. Default 55.",
                    "0 = 更透(看见桌面), 100 = 更实。默认 55。",
                    "0 = 更透(看見桌面), 100 = 更實。預設 55。"));
        ImGui::PopFont();
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
    NLToggle(i18n::t("Animated lyrics", "歌词动态效果", "歌詞動態效果"), &s.lyric_motion);
    CardEnd();

    SectionTitle(i18n::t("FORMAT BUILDER", "格式构建", "格式建構"));
    if (s.scroll_to_format) {
        ImGui::SetScrollHereY(0.f);
        s.scroll_to_format = false;
    }
    CardBegin("##card_fmt");
    ImGui::PushFont(font_caption);
    ImGui::TextColored(col::text_dim, "%s",
        i18n::t("Pick fields, reorder with arrows, toggle on/off. Preview updates live.",
                "勾选字段、用箭头排序、开关控制是否显示。预览会实时更新。",
                "勾選欄位、用箭頭排序、開關控制是否顯示。預覽會即時更新。"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(6.f)));

    // 两个场景用 tab 式切换,避免一页太长
    static int fmt_tab = 0;
    {
        const char* tabs_en[] = { "With lyrics", "No lyrics / early" };
        const char* tabs_sc[] = { "有歌词时", "无歌词 / 前奏" };
        const char* tabs_tc[] = { "有歌詞時", "無歌詞 / 前奏" };
        const char** tabs =
            s.language == i18n::Lang::SC ? tabs_sc :
            s.language == i18n::Lang::TC ? tabs_tc : tabs_en;
        float half = (ContentW() - S(8.f)) * 0.5f;
        if (NLButton(tabs[0], half, S(30.f), /*accent*/fmt_tab == 0)) fmt_tab = 0;
        ImGui::SameLine(0, S(8.f));
        if (NLButton(tabs[1], half, S(30.f), /*accent*/fmt_tab == 1)) fmt_tab = 1;
    }
    ImGui::Dummy(ImVec2(0, S(8.f)));

    if (fmt_tab == 0)
        DrawFmtBuilderEditor("##with", s.fmt_with_lyrics, /*for_lyrics*/true, s);
    else
        DrawFmtBuilderEditor("##none", s.fmt_without_lyrics, /*for_lyrics*/false, s);

    CardEnd();
}

void Draw(State& s, int win_w, int win_h) {
    i18n::current = s.language;
    interface_motion_enabled = s.ui_motion;
    TickThemeTransition(ImGui::GetIO().DeltaTime);

    const float top_h = S(41.f);
    const float sb_w  = S(190.f);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)win_w, top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
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
    // same chrome as VRC LYRICS title strip
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
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

    const float footer_h = s.current_tab == Tab::Lyrics ? S(32.f) : S(86.f);
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz  = ImGui::GetWindowSize();
    ImVec2 f0(wpos.x, wpos.y + wsz.y - footer_h);
    ImDrawList* dl_fg = ImGui::GetForegroundDrawList();
    ImVec2 av_c(f0.x + S(30.f), f0.y + S(34.f));

    float disc_r = S(22.f);
    if(s.np_detected && s.np_playing && s.ui_motion)
        s.cover_angle=std::fmod(s.cover_angle+ImClamp(ImGui::GetIO().DeltaTime,0.f,.05f)*.7f,2.f*IM_PI);
    s.cover_swap_anim = s.ui_motion ? ImMin(1.f, s.cover_swap_anim + ImGui::GetIO().DeltaTime / .24f) : 1.f;

    if (s.current_tab != Tab::Lyrics && s.cover_srv) {
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
        dl_fg->AddCircleFilled(av_c, S(3.5f), U32(col::bg_titlebar), 16);
    } else if (s.current_tab != Tab::Lyrics) {
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

    if (s.current_tab != Tab::Lyrics && s.np_detected) {
        char tbuf[128], abuf[128];
        truncate_utf8(s.np_title,  12, tbuf, sizeof(tbuf));
        truncate_utf8(s.np_artist, 14, abuf, sizeof(abuf));
        ImGui::PushFont(font_body);
        dl_fg->AddText(ImVec2(f0.x + S(62.f), f0.y + S(20.f)), U32(col::text), tbuf);
        ImGui::PopFont();
        ImGui::PushFont(font_caption);
        dl_fg->AddText(ImVec2(f0.x + S(62.f), f0.y + S(40.f)), U32(col::text_dim), abuf);
        ImGui::PopFont();
    } else if (s.current_tab != Tab::Lyrics) {
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
#ifdef VRC_UI_TEST
    const char* ver = "UI TEST";
#else
    const char* ver = "v3.4";
#endif
    ImVec2 vsz = ImGui::CalcTextSize(ver);
    dl_fg->AddText(ImVec2(f0.x + wsz.x - vsz.x - S(12.f), f0.y + footer_h - S(18.f)),
                   U32(col::text_dim), ver);
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    s.tab_transition = s.ui_motion ? ImMin(1.f, s.tab_transition + ImGui::GetIO().DeltaTime / .20f) : 1.f;
    const float page_ease=motion::Ease(s.tab_transition);
    const float page_offset=(1.f-page_ease)*S(6.f);
    const float page_alpha=.72f+.28f*page_ease;



    // The content surface also rounds inward at the sidebar seam (top and bottom).
    // Extend the sidebar behind those corners; no full-height straight seam overlay.
    {
        ImDrawList* dl_bg = ImGui::GetBackgroundDrawList();
        ImDrawList* dl_fg = ImGui::GetForegroundDrawList();
        const float rnd = S(16.f);
        // single left column color (titlebar == sidebar palette)
        dl_bg->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(sb_w, (float)win_h), U32(col::bg_titlebar));
        dl_bg->PathLineTo(ImVec2(sb_w,0));
        dl_bg->PathArcTo(ImVec2(sb_w+rnd,rnd),rnd,-IM_PI*.5f,-IM_PI,12);
        dl_bg->PathFillConvex(U32(col::bg_titlebar));
        dl_bg->PathLineTo(ImVec2(sb_w,(float)win_h));
        dl_bg->PathArcTo(ImVec2(sb_w+rnd,(float)win_h-rnd),rnd,IM_PI,IM_PI*.5f,12);
        dl_bg->PathFillConvex(U32(col::bg_titlebar));
        ImVec2 c0(sb_w, 0.f);
        ImVec2 c1((float)win_w, (float)win_h);
        const ImDrawFlags rcorn = ImDrawFlags_RoundCornersAll;
        dl_bg->AddRectFilled(c0, c1, U32(col::bg_content), rnd, rcorn);
        dl_fg->AddRect(c0, c1, U32(col::stroke, 0.90f), rnd, rcorn, S(1.0f));
    }

ImGui::SetNextWindowPos(ImVec2(sb_w, top_h));
    ImGui::SetNextWindowSize(ImVec2((float)win_w - sb_w, (float)win_h - top_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.f), S(22.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.f);
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
        static float s_last_written = -1.f;

        int tab_i = (int)s.current_tab;
        float max_y = cwin ? cwin->ScrollMax.y : 0.f;
        float cur_y = cwin ? cwin->Scroll.y : 0.f;

        if (!s_scroll_inited || tab_i != s_last_tab) {
            s_scroll_inited = true;
            s_last_tab = tab_i;
            s_scroll_vel = 0.f;
            s_scroll_tgt = 0.f;
            ImGui::SetScrollY(0.f);
            s_last_written = -1.f;
        } else if (s_last_written >= 0.f && std::fabs(cur_y - s_last_written) > S(2.f)) {
            // Respect programmatic navigation (e.g. Adjust format) instead of
            // pulling the next frame back to the old inertial scroll target.
            s_scroll_tgt = cur_y;
            s_scroll_vel = 0.f;
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
            s_last_written = next_y;
        } else if (cwin) {
            ImGui::SetScrollY(0.f);
            s_scroll_tgt = 0.f;
            s_scroll_vel = 0.f;
            s_last_written = 0.f;
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

    ImGuiWindow* page_window=ImGui::GetCurrentWindow();
    switch (s.current_tab) {
        case Tab::Lyrics:   DrawLyrics(s);   break;
        case Tab::Activity: DrawActivity(s); break;
        case Tab::Audio:    DrawAudio(s);    break;
        case Tab::Video:    DrawVideoTab(s); break;
        case Tab::Settings: DrawSettings(s); break;
    }

    // Transform the rendered page as one unit. Never animate padding/width:
    // stable layout avoids text reflow, changing scroll limits and card cascades.
    if (page_alpha < .999f) {
        for (ImGuiWindow* w : GImGui->Windows) {
            if (!w->Active || (w!=page_window && w->RootWindow!=page_window)) continue;
            for (ImDrawVert& v : w->DrawList->VtxBuffer) {
                v.pos.y += page_offset;
                const unsigned int alpha=(v.col>>IM_COL32_A_SHIFT)&0xff;
                v.col=(v.col & ~IM_COL32_A_MASK) | ((unsigned int)(alpha*page_alpha)<<IM_COL32_A_SHIFT);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

}
