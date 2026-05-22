#pragma once
#include "style.h"
#include "i18n/i18n.h"

namespace menu {

enum class Tab { Lyrics = 0, Activity = 1, Audio = 2, Settings = 3 };

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
    float audio_gain_db = -6.f;               // (*) -6 dB default leaves headroom for VRChat Opus
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
};

void Draw(State& s, int win_w, int win_h);

// Custom widgets matching the Neverlose look.
bool NLToggle(const char* label, bool* v);
bool NLSliderInt(const char* label, int* v, int v_min, int v_max);
void NLDivider();

}
