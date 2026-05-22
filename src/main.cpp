#include "host/win32_window.h"
#include "host/d3d11_context.h"
#include "host/imgui_host.h"
#include "host/tray.h"
#include "menu/menu.h"
#include "menu/style.h"
#include "osc/chatbox.h"
#include "playback/smtc.h"
#include "lyrics/lyrics_service.h"
#include "lyrics/lrc_parser.h"
#include "util/image.h"
#include "util/foreground.h"
#include "config/config.h"
#include "audio/relay.h"
#include "audio/devices.h"
#include "audio/process_find.h"
#include "audio/vbcable_installer.h"
#include "imgui.h"

#include <d3d11.h>
#include <windowsx.h>
#include <combaseapi.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

static void Log(const char* msg) {
    FILE* f = nullptr;
    fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // 必须在创建窗口前声明 Per-Monitor DPI 感知,否则 Windows 会对窗口做双线性
    // 放大,字会糊。老 Windows 上自动 fallback 到旧的 Per-Monitor。
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }

    // 让 Windows 别把我们当后台高效模式 CPU 节流 —— SMTC 轮询、歌词请求、
    // OSC 发送都依赖主线程稳定调度。再叫一次 SetThreadExecutionState 加保险。
    {
        PROCESS_POWER_THROTTLING_STATE pt{};
        pt.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        pt.StateMask   = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                              &pt, sizeof(pt));
    }
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    UINT dpi = GetDpiForSystem();
    menu::ui_scale = (float)dpi / 96.0f;

    Log("== start ==");
    host::Win32Window window;
    int w = (int)(780.f * menu::ui_scale);
    int h = (int)(540.f * menu::ui_scale);
    if (!window.Create(L"VRC Lyrics", w, h)) { Log("window failed"); return 1; }
    window.SetDragRegion((int)(40 * menu::ui_scale));
    window.SetTitleButtonZone((int)(180 * menu::ui_scale));
    Log("window ok");

    host::D3D11Context d3d;
    if (!d3d.Create(window.Hwnd())) { Log("d3d failed"); return 2; }
    Log("d3d ok");

    host::ImGuiHost gui;
    if (!gui.Init(window.Hwnd(), d3d.Device(), d3d.Context())) { Log("gui failed"); return 3; }
    Log("gui ok");

    menu::LoadFontsAndStyle();
    Log("style ok");

    host::TrayIcon tray;
    tray.OnShow = [&]() { window.Show(); };
    tray.OnQuit = [&]() { window.Quit(); };
    tray.Install(window.Hwnd(), L"VRC Lyrics");

    window.SetMessageHook([&](HWND h, UINT m, WPARAM w, LPARAM l, bool& handled) -> LRESULT {
        // 关窗时只藏到托盘,服务继续在后台跑。
        if (m == WM_CLOSE) {
            window.Hide();
            handled = true;
            return 0;
        }
        if (tray.HandleMessage(m, w, l)) { handled = true; return 0; }
        return gui.WndProc(h, m, w, l, handled);
    });

    menu::State menu_state;
    config::Load(menu_state);
    menu::ApplyTheme(menu_state.theme);
    Log("config loaded");
    int frame = 0;
    Log("entering loop");

    osc::Chatbox chatbox;
    if (!chatbox.Init()) Log("chatbox init failed");
    chatbox.SetTarget(menu_state.osc_host, menu_state.osc_port);
    chatbox.SetRateLimitMs(menu_state.rate_limit_ms);

    playback::SmtcWatcher smtc;
    if (!smtc.Start()) Log("smtc start failed");
    Log("smtc started");

    lyrics::Service lyrics;
    lyrics.Start();
    Log("lyrics service started");

    // Audio relay. COM apartment lives inside the relay worker thread.
    audio::Relay g_audio;
    auto last_dev_refresh = std::chrono::steady_clock::now() - std::chrono::hours(1);
    auto last_netease_check = std::chrono::steady_clock::now() - std::chrono::hours(1);

    // Installer runs on a detached thread; progress is published via atomics
    // into install_state, then mirrored into menu_state each frame.
    struct InstallState {
        std::atomic<int>   step{ -1 };
        std::atomic<float> fraction{ 0.f };
        std::mutex         msg_mtx;
        std::string        msg;
        std::atomic<bool>  running{ false };
    };
    auto install_state = std::make_shared<InstallState>();

    bool autostart_consumed = false;
    bool com_init_main = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    bool last_running = false;
    int  test_counter = 0;
    auto last_settings_sync = std::chrono::steady_clock::now();

    std::string current_cover_ncm;
    ID3D11ShaderResourceView* current_srv  = nullptr;
    ID3D11ShaderResourceView* previous_srv = nullptr;

    // 显示位置做单调钳位,过滤 SMTC 轮询带来的小幅回跳(避免秒数 53→54→53→54 闪烁)。
    std::string monotonic_id;
    int64_t     monotonic_pos_ms = 0;

    // 前台应用名查询要打开进程句柄,有点开销 —— 500ms 一次足够,不每帧查。
    auto last_fg_query = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    auto copy_safe = [](char* dst, size_t cap, const std::string& src) {
        if (cap == 0) return;
        size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
        std::memcpy(dst, src.data(), n);
        dst[n] = 0;
    };

    while (!window.Closed()) {
        window.PumpMessages();
        bool visible = window.Visible();
        if (visible && window.Resized()) {
            d3d.Resize(window.Width(), window.Height());
        }

        if (visible) gui.NewFrame();

        {
            auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now_pt - last_fg_query).count() > 500) {
                last_fg_query = now_pt;
                std::string app = util::ForegroundAppName();
                copy_safe(menu_state.foreground_app, sizeof(menu_state.foreground_app), app);
            }
        }

        std::string current_line;
        if (auto t = smtc.Current()) {
            menu_state.np_detected = true;
            menu_state.np_playing  = (t->status == playback::Status::Playing);
            copy_safe(menu_state.np_title,  sizeof(menu_state.np_title),  t->title);
            copy_safe(menu_state.np_artist, sizeof(menu_state.np_artist), t->artist);
            copy_safe(menu_state.np_album,  sizeof(menu_state.np_album),  t->album);
            copy_safe(menu_state.np_ncm_id, sizeof(menu_state.np_ncm_id), t->ncm_id);
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t pos = t->EffectivePositionMs(now_ms);
            if (t->ncm_id != monotonic_id) {
                monotonic_id = t->ncm_id;
                monotonic_pos_ms = pos;
            } else {
                int64_t delta = pos - monotonic_pos_ms;
                if (delta >= 0 || delta < -2000) monotonic_pos_ms = pos;
            }
            menu_state.np_pos_ms = (int)monotonic_pos_ms;
            menu_state.np_dur_ms = (int)t->duration_ms;

            lyrics.SetIncludeTranslation(menu_state.include_translation);
            lyrics.RequestNcm(t->ncm_id);

            // 切歌时:旧 SRV 留作淡出,新封面用 SMTC 缩略图字节重新解码上传。
            if (t->ncm_id != current_cover_ncm) {
                if (previous_srv) { previous_srv->Release(); previous_srv = nullptr; }
                previous_srv = current_srv;
                current_srv  = nullptr;
                current_cover_ncm = t->ncm_id;
                if (!t->thumbnail_bytes.empty()) {
                    current_srv = util::CreateCircularTexture(
                        d3d.Device(),
                        t->thumbnail_bytes.data(),
                        t->thumbnail_bytes.size(),
                        (int)(128 * menu::ui_scale));
                }
                menu_state.cover_swap_anim = 0.f;
            }
            menu_state.cover_srv      = (void*)current_srv;
            menu_state.cover_srv_prev = (void*)previous_srv;

            auto bundle = lyrics.Current();
            menu_state.np_has_lyrics = (bundle && bundle->has_lyrics() && bundle->ncm_id == t->ncm_id);
            if (menu_state.np_has_lyrics) {
                int idx = lyrics::FindCurrentLine(bundle->lines, pos);
                if (idx >= 0) current_line = bundle->lines[idx].text;
            }
            copy_safe(menu_state.np_current_line, sizeof(menu_state.np_current_line), current_line);
        } else {
            menu_state.np_detected = false;
            menu_state.np_has_lyrics = false;
            menu_state.np_current_line[0] = 0;
            lyrics.RequestNcm("");
        }

        if (visible) menu::Draw(menu_state, window.Width(), window.Height());

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_settings_sync).count() > 1000) {
            chatbox.SetTarget(menu_state.osc_host, menu_state.osc_port);
            chatbox.SetRateLimitMs(menu_state.rate_limit_ms);
            last_settings_sync = now;
        }
        if (menu_state.service_running && !last_running) {
            chatbox.ForceSend("\xE2\x96\xB6\xEF\xB8\x8F VRC Lyrics: service started");
        } else if (!menu_state.service_running && last_running) {
            chatbox.ForceSend("");
        }
        if (menu_state.service_running) {
            char buf[600];
            char prefix[96] = "";
            if (menu_state.show_foreground_app && menu_state.foreground_app[0]) {
                std::snprintf(prefix, sizeof(prefix), "\xF0\x9F\x8E\xAE %s \xC2\xB7 ", menu_state.foreground_app);
            }
            if (menu_state.np_detected && menu_state.np_has_lyrics && current_line.size()) {
                std::snprintf(buf, sizeof(buf),
                    "%s\xE2\x96\xB6\xEF\xB8\x8F %s - %s\n\xF0\x9F\x8E\xA4 %s",
                    prefix, menu_state.np_title, menu_state.np_artist, current_line.c_str());
            } else if (menu_state.np_detected) {
                int p = menu_state.np_pos_ms / 1000, d = menu_state.np_dur_ms / 1000;
                const char* icon = menu_state.np_playing
                    ? "\xE2\x96\xB6\xEF\xB8\x8F" : "\xE2\x8F\xB8\xEF\xB8\x8F";
                std::snprintf(buf, sizeof(buf), "%s%s %s - %s [%d:%02d / %d:%02d]",
                              prefix, icon, menu_state.np_title, menu_state.np_artist,
                              p / 60, p % 60, d / 60, d % 60);
            } else {
                std::snprintf(buf, sizeof(buf), "%s\xF0\x9F\x8E\xB5 VRC Lyrics test #%d",
                              prefix, test_counter);
            }
            if (chatbox.TrySend(buf)) test_counter++;
        }
        last_running = menu_state.service_running;

        // ---- Audio relay per-frame wiring ----
        {
            auto now_a = std::chrono::steady_clock::now();

            // Refresh render-device list every ~2s + VB-Cable installed flag.
            if (menu_state.audio_refresh_request ||
                std::chrono::duration_cast<std::chrono::milliseconds>(now_a - last_dev_refresh).count() > 2000) {
                last_dev_refresh = now_a;
                menu_state.audio_refresh_request = false;
                auto devs = audio::EnumRenderDevices();
                int n = (int)devs.size();
                if (n > 16) n = 16;
                menu_state.audio_device_count = n;
                bool installed = false;
                for (int i = 0; i < n; ++i) {
                    auto& d = devs[i];
                    int wn = WideCharToMultiByte(CP_UTF8, 0, d.id.c_str(), (int)d.id.size(),
                                                 menu_state.audio_devices[i].id,
                                                 sizeof(menu_state.audio_devices[i].id) - 1,
                                                 nullptr, nullptr);
                    menu_state.audio_devices[i].id[wn] = 0;
                    copy_safe(menu_state.audio_devices[i].label,
                              sizeof(menu_state.audio_devices[i].label),
                              d.friendly_utf8);
                    menu_state.audio_devices[i].is_vbcable = d.is_vbcable;
                    menu_state.audio_devices[i].is_default = d.is_default;
                    if (d.is_vbcable) installed = true;
                }
                menu_state.audio_vbcable_installed = installed;
            }

            // Refresh Netease detection every ~1s.
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now_a - last_netease_check).count() > 1000) {
                last_netease_check = now_a;
                menu_state.audio_netease_detected = audio::FindNeteaseRoot().has_value();
            }

            // Install request.
            if (menu_state.audio_install_request && !install_state->running.load()) {
                menu_state.audio_install_request = false;
                install_state->running.store(true);
                install_state->step.store(0);
                install_state->fraction.store(0.f);
                {
                    std::lock_guard<std::mutex> lk(install_state->msg_mtx);
                    install_state->msg = "Starting...";
                }
                auto st = install_state;
                std::thread([st]() {
                    audio::vbcable::Install([st](const audio::InstallProgress& p) {
                        st->step.store((int)p.step);
                        st->fraction.store(p.fraction);
                        std::lock_guard<std::mutex> lk(st->msg_mtx);
                        st->msg = p.message;
                    });
                    st->running.store(false);
                }).detach();
            }
            // Mirror installer state to menu_state for UI.
            menu_state.audio_install_step     = install_state->step.load();
            menu_state.audio_install_fraction = install_state->fraction.load();
            {
                std::lock_guard<std::mutex> lk(install_state->msg_mtx);
                copy_safe(menu_state.audio_install_msg, sizeof(menu_state.audio_install_msg),
                          install_state->msg);
            }

            // Push live tweaks into the running relay.
            g_audio.SetGainDb(menu_state.audio_gain_db);
            g_audio.SetLimiter(menu_state.audio_limiter);

            // Start / Stop requests.
            if (menu_state.audio_start_request) {
                menu_state.audio_start_request = false;
                audio::RelayConfig cfg;
                if (menu_state.audio_target_device_id[0]) {
                    std::string id8 = menu_state.audio_target_device_id;
                    int wn = MultiByteToWideChar(CP_UTF8, 0, id8.c_str(), (int)id8.size(),
                                                 nullptr, 0);
                    cfg.target_device_id.assign(wn, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, id8.c_str(), (int)id8.size(),
                                        cfg.target_device_id.data(), wn);
                }
                cfg.gain_db = menu_state.audio_gain_db;
                cfg.limiter = menu_state.audio_limiter;
                g_audio.Start(cfg);
            }
            if (menu_state.audio_stop_request) {
                menu_state.audio_stop_request = false;
                g_audio.Stop();
            }

            // Pull status.
            auto st = g_audio.GetStatus();
            menu_state.audio_relay_running = st.running;
            menu_state.audio_peak_dbfs     = st.peak_dbfs;
            if (!st.error_text.empty()) {
                copy_safe(menu_state.audio_status_text,
                          sizeof(menu_state.audio_status_text), st.error_text);
            } else {
                copy_safe(menu_state.audio_status_text,
                          sizeof(menu_state.audio_status_text), st.status_text);
            }

            // Autostart: once per session, only when fully ready.
            if (menu_state.audio_autostart && !autostart_consumed &&
                menu_state.audio_vbcable_installed && menu_state.audio_netease_detected &&
                !menu_state.audio_relay_running) {
                autostart_consumed = true;
                menu_state.audio_start_request = true;
            }
        }

        if (menu_state.save_request) {
            menu_state.save_request = false;
            if (config::Save(menu_state)) {
                menu_state.save_toast_sec = 1.6f;
                Log("config saved");
            } else {
                Log("config save FAILED");
            }
        }

        if (visible) {
            const float clear[4] = { 0.082f, 0.098f, 0.122f, 1.f };
            d3d.BeginFrame(clear);
            gui.Render();
            d3d.Present(false);
            // 每帧 InvalidateRect 强制 WM_PAINT 入队,免得 Windows 把我们当空闲进程降帧。
            InvalidateRect(window.Hwnd(), nullptr, FALSE);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (frame < 3 || (frame % 120 == 0)) {
            char b[64]; sprintf_s(b, "frame %d ok visible=%d", frame, visible ? 1 : 0);
            Log(b);
        }
        ++frame;
    }
    Log("loop exited");

    chatbox.ForceSend("");
    config::Save(menu_state);   // auto-save on quit
    g_audio.Stop();
    lyrics.Stop();
    smtc.Stop();
    tray.Remove();
    chatbox.Shutdown();
    if (current_srv)  current_srv->Release();
    if (previous_srv) previous_srv->Release();
    gui.Shutdown();   Log("gui shutdown ok");
    d3d.Destroy();    Log("d3d destroy ok");
    window.Destroy(); Log("window destroy ok");
    if (com_init_main) CoUninitialize();
    return 0;
}
