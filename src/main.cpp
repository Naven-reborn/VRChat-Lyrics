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
#include "bilibili/parser.h"
#include "imgui.h"
#ifdef VRC_UI_TEST
#include "host/ui_test.h"
#include "bilibili/parser_checks.h"
#endif

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
#include <vector>
#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

static void Log(const char* msg) {
    FILE* f = nullptr;
    fopen_s(&f, "vrc-lyrics.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR args, int) {
#ifdef VRC_UI_TEST
    if (wcsstr(args,L"--bili-network-check")) return bilibili::NetworkChecks();
    if (wcsstr(args, L"--ui-capture")) return host::RunUiCapture(args);
#endif
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
    int w = (int)(1000.f * menu::ui_scale);
    int h = (int)(680.f * menu::ui_scale);
    RECT work_area{};
    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &work_area, 0)) {
        w = (std::min)(w, (int)(work_area.right-work_area.left)-32);
        h = (std::min)(h, (int)(work_area.bottom-work_area.top)-32);
    }
    if (!window.Create(L"VRC Lyrics 3.4", w, h)) { Log("window failed"); return 1; }
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

    menu::State menu_state;
#ifdef VRC_UI_TEST
    menu_state.language = i18n::Lang::SC;
    menu_state.theme = menu::Theme::Blur;
    menu_state.include_translation = true;
    menu_state.minimize_to_tray = false;
#endif
    config::Load(menu_state);
#ifdef VRC_UI_TEST
    // Separate settings; never automatically broadcast audio in the test build.
    menu_state.audio_autostart = false;
#endif
    BOOL animations_enabled = TRUE;
    if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animations_enabled, 0) && !animations_enabled)
        menu_state.lyric_motion = menu_state.ui_motion = false;
    // theme 枚举只认 0..2;旧 config / 手改可能写出越界值。
    if ((int)menu_state.theme < 0 || (int)menu_state.theme > 2)
        menu_state.theme = menu::Theme::Dark;

    // composition swapchain 启动就绑好,主题切换只改:
    //   1) DWM Acrylic on/off
    //   2) 调色板(Blur 大面透明 / Dark·Light 不透明)
    //   3) clear alpha
    // 不再销毁 HWND swapchain —— 那是"旧主题垫底"的根因。
    if (!d3d.CompositionActive()) {
        Log("composition swapchain missing — blur unavailable");
    }

    // Blur clear tint: light frost under panels (panels themselves carry most opacity).
    // Keep alpha modest so Acrylic still reads as glass, not a solid sheet.
    auto make_clear = [&](bool blur, float clear[4]) {
        if (blur) {
            // premultiplied clear tint; alpha scales with blur_opacity (default 55 → ~0.27)
            const float a = menu::BlurClearAlpha(menu_state.blur_opacity);
            clear[0] = (16.f / 255.f) * a;
            clear[1] = (18.f / 255.f) * a;
            clear[2] = (24.f / 255.f) * a;
            clear[3] = a;
        } else {
            clear[0] = menu::col::bg_content.x;
            clear[1] = menu::col::bg_content.y;
            clear[2] = menu::col::bg_content.z;
            clear[3] = 1.f;
        }
    };
    auto flush_clear = [&](bool blur) {
        float clear[4];
        make_clear(blur, clear);
        d3d.ClearAllBuffers(clear);
    };

    auto apply_backdrop = [&](menu::Theme t) -> menu::Theme {
        const bool want = menu::ThemeWantsBlur(t);
        if (want) {
            if (!d3d.CompositionActive()) {
                Log("blur requested but composition inactive - fall back Dark");
                window.SetAcrylicBlur(false);
                return menu::Theme::Dark;
            }
            if (!window.SetAcrylicBlur(true)) {
                Log("acrylic FAILED - fall back Dark");
                return menu::Theme::Dark;
            }
            d3d.SetPremultipliedAlpha(true);
            Log("acrylic on (composition kept)");
            return t;
        }
        window.SetAcrylicBlur(false);
        d3d.SetPremultipliedAlpha(false);
        return t;
    };

    menu_state.theme = apply_backdrop(menu_state.theme);
    menu::SnapTheme(menu_state.theme, menu_state.blur_opacity);
    flush_clear(menu::ThemeWantsBlur(menu_state.theme));
    menu::Theme applied_theme = menu_state.theme;
    int last_blur_opacity = menu_state.blur_opacity;
    Log("config loaded");

    window.SetMessageHook([&](HWND h, UINT m, WPARAM w, LPARAM l, bool& handled) -> LRESULT {
        // 关窗:设置里 "关闭时最小化到托盘" 开着 → 藏托盘继续跑;
        // 关掉 → 真正退出。
        // 重要:无论哪条路径都 handled=true 并 return 0,阻止 DefWindowProc
        // 走默认 DestroyWindow。真正的资源释放在主循环退出后顺序做,
        // 否则 WM_DESTROY 进消息泵 + 主线程还在 join 后台线程 = 假死未响应。
        if (m == WM_CLOSE) {
            if (menu_state.minimize_to_tray) {
                window.Hide();
            } else {
                // 立刻藏起来,用户不会看到"未响应"灰窗;后台线程在 loop 退出后停。
                window.Hide();
                window.Quit();   // 置 m_closed,主循环下一轮退出
            }
            handled = true;
            return 0;
        }
        if (tray.HandleMessage(m, w, l)) { handled = true; return 0; }
        return gui.WndProc(h, m, w, l, handled);
    });

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

    // Bilibili 解析:同样的"worker 写 atomic+mutex,主循环每帧镜像到 menu_state"模式。
    // 用 shared_ptr 让 detached worker 独立持有,主线程退出时不会 dangling。
    struct VideoParseState {
        std::atomic<bool> running{false};
        std::mutex mu;
        bilibili::ParseResult result;
        unsigned revision=0;
    };
    auto video_state = std::make_shared<VideoParseState>();

    auto copy_to_clipboard = [&](const char* utf8) -> bool {
        if (!utf8 || !*utf8) return false;
        int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (wn <= 0) return false;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wn * sizeof(wchar_t));
        if (!hg) return false;
        auto* p = (wchar_t*)GlobalLock(hg);
        if (!p) { GlobalFree(hg); return false; }
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, p, wn);
        GlobalUnlock(hg);
        HWND hw = window.Hwnd();
        if (!OpenClipboard(hw)) { GlobalFree(hg); return false; }
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, hg)) {
            // 失败时 hg 还归我们所有,得手动释放。
            GlobalFree(hg);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    };

    bool autostart_consumed = false;
    bool com_init_main = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    bool last_running = false;
    int  test_counter = 0;
    auto last_settings_sync = std::chrono::steady_clock::now();

    std::string current_cover_ncm;
    ID3D11ShaderResourceView* current_srv  = nullptr;
    ID3D11ShaderResourceView* square_srv = nullptr;
    ID3D11ShaderResourceView* previous_srv = nullptr;

    // 显示位置做单调钳位,过滤 SMTC 外推/采样抖动带来的小幅回跳
    // (避免秒数 53→54→53→54 闪烁,以及歌词行来回跳)。
    // 只有真正的 seek(回退 > 1.5s)或切歌才允许位置往回走。
    std::string monotonic_id;
    int64_t     monotonic_pos_ms = 0;
    playback::Status monotonic_status = playback::Status::Stopped;

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
                auto fg = util::ForegroundApp();
                copy_safe(menu_state.foreground_app, sizeof(menu_state.foreground_app), fg.name);
                menu_state.foreground_category = (int)fg.category;
            }
            // 键鼠空闲秒数,每帧查也很便宜(就一个 GetLastInputInfo 系统调用)。
            menu_state.idle_seconds = util::IdleSeconds();

            // status_override 倒计时:只在 clear_min>0 且文本非空时滴答。
            // 每帧扣一次太快,按 DeltaTime 累积。这里用一个静态累加器,
            // 避免每帧都做整数运算,精度到秒就够了。
            static double override_tick_acc = 0.0;
            if (menu_state.status_override[0] && menu_state.status_override_clear_min > 0) {
                override_tick_acc += ImGui::GetIO().DeltaTime;
                while (override_tick_acc >= 1.0 && menu_state.status_override_remaining_sec > 0) {
                    menu_state.status_override_remaining_sec -= 1;
                    override_tick_acc -= 1.0;
                }
                if (menu_state.status_override_remaining_sec <= 0) {
                    menu_state.status_override[0]       = 0;
                    menu_state.status_override_emoji[0] = 0;
                    override_tick_acc = 0.0;
                }
            } else {
                override_tick_acc = 0.0;
            }
        }

        std::string current_line;
        if (auto t = smtc.Current()) {
            menu_state.np_detected = true;
            menu_state.np_playing  = (t->status == playback::Status::Playing);
            menu_state.np_source   = (int)t->source;
            menu_state.np_track_key = t->match_key;
            copy_safe(menu_state.np_title,  sizeof(menu_state.np_title),  t->title);
            copy_safe(menu_state.np_artist, sizeof(menu_state.np_artist), t->artist);
            copy_safe(menu_state.np_album,  sizeof(menu_state.np_album),  t->album);
            copy_safe(menu_state.np_ncm_id, sizeof(menu_state.np_ncm_id), t->ncm_id);
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t pos = t->EffectivePositionMs(now_ms);
            // 用 match_key 做切歌识别,跨 source 都稳。
            if (t->match_key != monotonic_id) {
                monotonic_id     = t->match_key;
                monotonic_pos_ms = pos;
                monotonic_status = t->status;
            } else {
                // 暂停/停止:直接采信当前值。
                // 播放中:
                //   - 浏览器粘滞源(YT Music 网页):几乎只允许前进,小回跳全吞掉;
                //     seek 阈值放到 3s,避免被卡死的 raw 把进度拽回十几秒前。
                //   - 其它源:小回跳(<1.5s)过滤,大回退当 seek。
                if (t->status != playback::Status::Playing) {
                    monotonic_pos_ms = pos;
                } else {
                    int64_t delta = pos - monotonic_pos_ms;
                    const int64_t seek_back = t->sticky_timeline ? -3000 : -1500;
                    if (delta >= 0 || delta < seek_back) monotonic_pos_ms = pos;
                    // 从暂停恢复播放时,允许立刻对齐到外推位置(即使略回退)。
                    if (monotonic_status != playback::Status::Playing &&
                        t->status == playback::Status::Playing) {
                        monotonic_pos_ms = pos;
                    }
                    // 粘滞源播放中:若外推位置比钳位位置超前,每帧跟上去
                    // (避免 monotonic 卡在旧值而 Effective 已经往前走)。
                    if (t->sticky_timeline && pos > monotonic_pos_ms) {
                        monotonic_pos_ms = pos;
                    }
                }
                monotonic_status = t->status;
            }
            // 歌词行和 UI 进度条统一用钳位后的位置,避免两套时钟打架。
            pos = monotonic_pos_ms;
            menu_state.np_pos_ms = (int)pos;
            menu_state.np_dur_ms = (int)t->duration_ms;

            lyrics.SetIncludeTranslation(menu_state.include_translation);
            lyrics.SetProvider((lyrics::Provider)menu_state.lyrics_provider);
            lyrics::Query lreq;
            lreq.match_key    = t->match_key;
            lreq.ncm_id       = t->ncm_id;
            lreq.title        = t->title;
            lreq.artist       = t->artist;
            lreq.album        = t->album;
            lreq.duration_sec = (int)(t->duration_ms / 1000);
            lreq.is_netease   = (t->source == playback::Source::NetEase);
            lyrics.Request(lreq);

            // 切歌时:旧 SRV 留作淡出,新封面用 SMTC 缩略图字节重新解码上传。
            if (t->match_key != current_cover_ncm) {
                if (previous_srv) { previous_srv->Release(); previous_srv = nullptr; }
                previous_srv = current_srv;
                current_srv  = nullptr;
                if (square_srv) { square_srv->Release(); square_srv = nullptr; }
                current_cover_ncm = t->match_key;
                if (!t->thumbnail_bytes.empty()) {
                    current_srv = util::CreateCircularTexture(
                        d3d.Device(),
                        t->thumbnail_bytes.data(),
                        t->thumbnail_bytes.size(),
                        (int)(128 * menu::ui_scale));
                    square_srv = util::CreateCircularTexture(d3d.Device(),
                        t->thumbnail_bytes.data(), t->thumbnail_bytes.size(),
                        (int)(160 * menu::ui_scale), false);
                }
                menu_state.cover_swap_anim = 0.f;
            }
            menu_state.cover_srv      = (void*)current_srv;
            menu_state.cover_srv_prev = (void*)previous_srv;
            menu_state.cover_square_srv = square_srv;

            auto bundle = lyrics.Current();
            menu_state.np_has_lyrics = (bundle && bundle->has_lyrics() && bundle->match_key == t->match_key);
            if (menu_state.np_has_lyrics) {
                int idx = lyrics::FindCurrentLine(bundle->lines, pos);
                if (idx >= 0) current_line = bundle->lines[idx].text;
            }
            copy_safe(menu_state.np_current_line, sizeof(menu_state.np_current_line), current_line);
        } else {
            menu_state.np_detected = false;
            menu_state.np_has_lyrics = false;
            menu_state.np_current_line[0] = 0;
            menu_state.np_source = 0;
            menu_state.np_track_key.clear();
            menu_state.np_playing = false;
            menu_state.np_pos_ms = menu_state.np_dur_ms = 0;
            menu_state.cover_square_srv = nullptr;
            lyrics::Query empty; lyrics.Request(empty);  // clear
        }

        // 同步主题 → Acrylic / clear 语义。
        // 颜色过渡由 menu::BeginThemeTransition + TickThemeTransition 负责;
        // 这里只在 theme 枚举变化时切 DWM Acrylic,并刷掉 flip 缓冲残帧。
        // Draw 前跑一次;Draw 后再跑一次,接住本帧标题栏/设置里的点击。
        auto sync_theme = [&]() {
            if (menu_state.theme == applied_theme) return;
            menu::Theme resolved = apply_backdrop(menu_state.theme);
            if (resolved != menu_state.theme) {
                // Acrylic 不可用:打断 UI 过渡,落到 Dark。
                menu_state.theme = resolved;
                menu::SnapTheme(resolved, menu_state.blur_opacity);
            }
            // 不在这里 SnapTheme 成功路径 —— 保留 BeginThemeTransition 的 300ms lerp。
            flush_clear(menu::ThemeWantsBlur(resolved) || window.AcrylicEnabled());
            applied_theme = menu_state.theme;
        };
        sync_theme();

        if (visible) menu::Draw(menu_state, window.Width(), window.Height());
        sync_theme();
        // dragging blur opacity: next frame make_clear uses new alpha
        if (menu_state.theme == menu::Theme::Blur) {
            last_blur_opacity = menu_state.blur_opacity;
        } else {
            last_blur_opacity = menu_state.blur_opacity;
        }

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
            // 用 menu 的统一逻辑算前缀(override > AFK > 前台应用)。
            std::string prefix_s = menu::EffectiveStatusPrefix(menu_state);

            // 暂停时是否仍推音乐:受 send_while_paused 控制。
            const bool music_ok = menu_state.np_detected &&
                (menu_state.np_playing || menu_state.send_while_paused);

            std::string msg;
            if (music_ok) {
                const bool has_line = menu_state.np_has_lyrics && !current_line.empty();
                msg = menu::RenderChatbox(
                    menu_state,
                    prefix_s.c_str(),
                    menu_state.np_playing,
                    has_line,
                    current_line.c_str());
            } else if (!prefix_s.empty()) {
                // 没音乐(或暂停且不允许发送)但有状态前缀:
                // 只发状态那一行,把末尾 " · " 切掉。
                std::string trimmed = prefix_s;
                if (trimmed.size() >= 4 &&
                    trimmed.compare(trimmed.size() - 4, 4, " \xC2\xB7 ") == 0) {
                    trimmed.resize(trimmed.size() - 4);
                }
                msg = std::move(trimmed);
            }
            // 完全没内容:静默,不刷 test 计数。

            if (!msg.empty()) {
                if (chatbox.TrySend(msg)) test_counter++;
            }
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

        // Parser results are published once, with options captured before dispatch.
        {
            static unsigned received_revision=0;
            if(menu_state.video_parse_request && !video_state->running.load()) {
                menu_state.video_parse_request=false;
                const std::string input=menu_state.video_input;
                bilibili::ParseOptions options;
                if(!menu_state.video_input_changed) {
                    const auto& pages=menu_state.video_result.pages;
                    if(menu_state.video_page_index>=0 && menu_state.video_page_index<(int)pages.size())
                        options.page=pages[menu_state.video_page_index].number;
                    const auto& qualities=menu_state.video_result.qualities;
                    const int qi=menu_state.video_quality_index-1;
                    if(qi>=0 && qi<(int)qualities.size())options.quality=qualities[qi].id;
                }
                menu_state.video_status=1;
                menu_state.video_result_url[0]=0;
                menu_state.video_last_input=input;
                video_state->running.store(true);
                auto st=video_state;
                std::thread([st,input,options] {
                    auto result=bilibili::Parse(input,options);
                    { std::lock_guard<std::mutex> lock(st->mu);st->result=std::move(result);++st->revision; }
                    st->running.store(false);
                }).detach();
            }
            {
                std::lock_guard<std::mutex> lock(video_state->mu);
                if(received_revision!=video_state->revision) {
                    received_revision=video_state->revision;
                    // Editing the input during a request must not publish stale results.
                    if(menu_state.video_last_input==menu_state.video_input) {
                        menu_state.video_result=video_state->result;
                        const auto& result=menu_state.video_result;
                        menu_state.video_status=result.ok?2:3;
                        menu_state.video_input_changed=false;
                        menu_state.video_stream_index=0;
                        menu_state.video_page_index=0;
                        menu_state.video_quality_index=0;
                        for(size_t i=0;i<result.qualities.size();++i)
                            if(result.qualities[i].id==result.requested_quality)menu_state.video_quality_index=(int)i+1;
                        for(size_t i=0;i<result.pages.size();++i)
                            if(result.pages[i].number==result.page)menu_state.video_page_index=(int)i;
                        copy_safe(menu_state.video_result_url,sizeof(menu_state.video_result_url),result.url);
                        copy_safe(menu_state.video_result_title,sizeof(menu_state.video_result_title),result.title);
                        copy_safe(menu_state.video_error,sizeof(menu_state.video_error),result.message);
                    } else { menu_state.video_status=0; }
                }
            }
            if(menu_state.video_copy_request) {
                menu_state.video_copy_request=false;
                copy_to_clipboard(menu_state.video_result_url);
            }
            if(menu_state.video_copy_source_request) {
                menu_state.video_copy_source_request=false;
                copy_to_clipboard(menu_state.video_result.source_url.c_str());
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
            // clear 跟调色板一起过渡(ThemeTransitionT 0→1)。
            // acrylic 在切主题瞬间已切换,所以 blur_now = 目标是否 Blur。
            float clear[4];
            const bool blur_now = window.AcrylicEnabled();
            if (menu::ThemeTransitionActive()) {
                float a_from[4], a_to[4];
                make_clear(!blur_now, a_from); // 上一主题
                make_clear(blur_now,  a_to);   // 目标主题
                const float t = menu::ThemeTransitionT();
                for (int i = 0; i < 4; ++i)
                    clear[i] = a_from[i] + (a_to[i] - a_from[i]) * t;
            } else {
                make_clear(blur_now, clear);
            }
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

    // 退出顺序很关键:
    //  1) 先摘托盘,避免退出过程中用户再点图标
    //  2) 停音频 / 歌词 / SMTC(可能 join 几百 ms,窗口此时已 Hide,不会灰屏)
    //  3) 再存配置、释放 GPU / GUI / 窗口
    // 绝不要在 WM_CLOSE 回调里 join 线程。
    tray.Remove();
    Log("tray removed");


    try { chatbox.ForceSend(""); } catch (...) {}
    try { g_audio.Stop(); } catch (...) {}
    Log("audio stopped");

    try { lyrics.Stop(); } catch (...) {}
    Log("lyrics stopped");

    try { smtc.Stop(); } catch (...) {}
    Log("smtc stopped");

    try { config::Save(menu_state); } catch (...) {}  // auto-save on quit
    Log("config saved on quit");

    try { chatbox.Shutdown(); } catch (...) {}
    if (current_srv)  { current_srv->Release();  current_srv  = nullptr; }
    if (previous_srv) { previous_srv->Release(); previous_srv = nullptr; }
    if (square_srv) { square_srv->Release(); square_srv = nullptr; }
    try { gui.Shutdown(); } catch (...) {}
    Log("gui shutdown ok");
    try { d3d.Destroy(); } catch (...) {}
    Log("d3d destroy ok");
    try { window.Destroy(); } catch (...) {}
    Log("window destroy ok");
    if (com_init_main) CoUninitialize();
    Log("== exit ==");
    return 0;
}
