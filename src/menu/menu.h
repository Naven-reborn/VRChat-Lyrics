#pragma once
#include "style.h"
#include "i18n/i18n.h"
#include <cstdint>
#include <string>

namespace menu {

enum class Tab { Lyrics = 0, Activity = 1, Audio = 2, Video = 3, Settings = 4 };

struct State {
    Tab current_tab = Tab::Lyrics;
    Tab last_tab    = Tab::Lyrics;
    float tab_transition = 1.f;

    i18n::Lang language = i18n::Lang::EN;
    Theme      theme    = Theme::Dark;

    // Home tab (placeholder bindings — will move to AppState later)
    bool service_running = false;
    bool send_while_paused = true;
    bool show_foreground_app = true;       // append "🎮 AppName" to chatbox
    char foreground_app[64] = "";          // updated by main.cpp each frame
    int  foreground_category = 0;          // util::AppCategory enum value
    uint32_t idle_seconds = 0;             // GetLastInputInfo, updated each frame

    // Status override — session-only (会话级,不写 config)。优先级最高,顶掉
    // AFK 和前台应用,直到 _remaining_sec 走到 0 或用户清空。
    char status_override[96]       = "";
    char status_override_emoji[8]  = "";   // UTF-8,如 "\xF0\x9F\x92\xA4"
    int  status_override_clear_min = 0;    // 0=永久 / 5 / 30 / 60
    int  status_override_remaining_sec = 0; // 倒计时,0 且 clear_min>0 时清空

    // AFK 自动:键鼠空闲超过 threshold 分钟,chatbox 前缀显示 "💤 AFK"。
    bool afk_auto = true;                  // 持久化
    int  afk_threshold_min = 5;            // 持久化

    // 分类前缀图标,每类一个 UTF-8 emoji(用户可在 UI 里从 4 个候选改)。持久化。
    char emoji_game[8]    = "\xF0\x9F\x8E\xAE"; // 🎮
    char emoji_browser[8] = "\xF0\x9F\x8C\x90"; // 🌐
    char emoji_chat[8]    = "\xF0\x9F\x92\xAC"; // 💬
    char emoji_dev[8]     = "\xF0\x9F\x92\xBB"; // 💻
    char emoji_music[8]   = "\xF0\x9F\x8E\xB5"; // 🎵
    char emoji_office[8]  = "\xF0\x9F\x93\x84"; // 📄
    char emoji_stream[8]  = "\xF0\x9F\x8E\xAC"; // 🎬

    // Live playback view — pushed in each frame from main.cpp.
    bool        np_detected   = false;
    bool        np_playing    = false;
    char        np_title[256] = "not detected";
    char        np_artist[256]= "";
    char        np_album[256] = "";
    char        np_ncm_id[64] = "";
    int         np_pos_ms     = 0;
    int         np_dur_ms     = 0;
    bool        np_has_lyrics = false;
    char        np_current_line[512] = "";

    // Album cover. main.cpp owns the ID3D11ShaderResourceView's lifetime.
    void* cover_srv = nullptr;
    void* cover_srv_prev = nullptr; // for fade-out during song change
    float cover_angle = 0.f;        // radians, accumulated
    float cover_swap_anim = 1.f;    // 0 = just swapped (crossfade in), 1 = settled

    // Save toolbar wiring. menu.cpp sets save_request=true on click; main.cpp
    // sees it, calls config::Save, then resets to false and bumps save_toast.
    bool  save_request   = false;
    float save_toast_sec = 0.f;     // counts down to 0; > 0 means show "Saved"

    // Settings tab
    char osc_host[64] = "127.0.0.1";
    int  osc_port = 9000;
    int  rate_limit_ms = 1300;
    int  lyrics_provider = 0; // 0=Netease only, 1=Netease then LRCLib, 2=LRCLib only
    bool include_translation = false;
    bool strip_metadata_tags = true;
    char fmt_lyrics[256]    = "{status} {name} - {artist}\n{mic} {lyrics}";
    char fmt_no_lyrics[256] = "{status} {name} - {artist}";
    char fmt_paused[256]    = "{status} {name} - {artist}";

    // Audio relay tab — most fields are runtime-only; only the four marked
    // (*) are persisted by config.cpp.
    bool  audio_vbcable_installed = false;
    bool  audio_netease_detected  = false;
    bool  audio_relay_running     = false;
    char  audio_target_device_id[256] = "";   // (*) UTF-8 of wide endpoint id
    char  audio_target_device_label[128] = "";
    float audio_gain_db = -3.f;               // (*) -3 dB default — hotter signal helps Opus SNR through VRChat's voice codec
    bool  audio_limiter = true;               // (*) on by default — last-line brick wall, not a compressor
    bool  audio_autostart = false;            // (*)
    char  audio_status_text[128] = "";
    float audio_peak_dbfs = -120.f;
    bool  audio_install_request = false;
    bool  audio_start_request   = false;
    bool  audio_stop_request    = false;
    bool  audio_refresh_request = false;
    int   audio_install_step    = -1;  // matches audio::InstallStep
    float audio_install_fraction = 0.f;
    char  audio_install_msg[128] = "";

    // Render device list populated by main.cpp every ~2s. Decoupled from
    // audio/devices.h so menu.cpp doesn't pull in COM headers.
    struct AudioDeviceUI {
        char id[256];
        char label[128];
        bool is_vbcable;
        bool is_default;
    };
    AudioDeviceUI audio_devices[16] = {};
    int           audio_device_count = 0;

    // Video parser tab. main.cpp 拉一个 worker 跑 bilibili::Parse,
    // 解析期间 video_status=1,完成后填 video_result_url + video_status=2/3。
    char  video_input[512]      = "";    // 用户输入(BV / URL / b23.tv)
    char  video_result_url[4096]= "";    // 最终直链,UI 显示 + 复制用
    char  video_result_title[256]= "";   // 视频标题
    char  video_result_meta[128] = "";   // "1440P · DASH · upos-sz-..."
    char  video_error[128]      = "";    // 失败时的本地化提示
    int   video_status          = 0;     // 0=idle 1=parsing 2=ok 3=error
    bool  video_parse_request   = false;
    bool  video_copy_request    = false;
    float video_copy_toast_sec  = 0.f;   // 复制成功 toast 倒计时
};

void Draw(State& s, int win_w, int win_h);

// 计算当前应该挂在 chatbox 前面的状态前缀(含 emoji 和分隔符,如 "🎮 VRChat · ")。
// 优先级:status_override > AFK > 前台应用。空串表示不挂任何前缀。
// UI 预览和 main.cpp 构造 chatbox 都调这个,保证两边显示一致。
std::string EffectiveStatusPrefix(const State& s);

// Custom widgets matching the Neverlose look.
bool NLToggle(const char* label, bool* v);
bool NLSliderInt(const char* label, int* v, int v_min, int v_max);
void NLDivider();

}
