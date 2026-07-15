#include "smtc.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <string>

namespace playback {

namespace winrt_ns = winrt::Windows::Media::Control;

static std::string Utf8FromHstring(winrt::hstring const& h) {
    if (h.empty()) return {};
    int wlen = (int)h.size();
    int u8len = WideCharToMultiByte(CP_UTF8, 0, h.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    std::string r(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, h.c_str(), wlen, r.data(), u8len, nullptr, nullptr);
    return r;
}

static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string ToLowerAscii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

static bool StringEndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && std::memcmp(s.data() + s.size() - n, suffix, n) == 0;
}

static bool StringContains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

static bool IsBrowserAumid(const std::string& aumid_lower) {
    return StringContains(aumid_lower, "chrome")  || StringContains(aumid_lower, "msedge") ||
           StringContains(aumid_lower, "firefox") || StringContains(aumid_lower, "opera")  ||
           StringContains(aumid_lower, "brave")   || StringContains(aumid_lower, "vivaldi") ||
           StringContains(aumid_lower, "chromium");
}

// 浏览器媒体会话标题里常见的 YT Music 尾巴。大小写不敏感。
static bool LooksLikeYtMusicTitle(const std::string& title) {
    std::string t = ToLowerAscii(title);
    // 常见: "Song - Artist - YouTube Music"
    // 也见: "Song • Artist • YouTube Music" / "… | YouTube Music"
    return StringContains(t, "youtube music") ||
           StringContains(t, "youtubemusic");
}

// 根据 SourceAppUserModelId + Title 判断音乐源。识别策略:
//   - inflink-rs 插件写 Genres "NCM-..." → NetEase(在外层先匹配掉)
//   - SourceAppUserModelId 包含 "Spotify" → Spotify
//   - SourceAppUserModelId 包含 "YouTubeMusic"(YTMD 桌面端) → YTMusic
//   - 浏览器 + 标题含 "YouTube Music" → YTMusic(网页端 music.youtube.com)
//   - 其它一律 Other(查不到歌词,但仍发"播放中"信息)
static Source ClassifySource(const std::string& aumid_in, const std::string& title) {
    std::string aumid = ToLowerAscii(aumid_in);

    if (StringContains(aumid, "spotify")) return Source::Spotify;
    if (StringContains(aumid, "youtubemusic") ||
        StringContains(aumid, "ytmdesktop")    ||
        StringContains(aumid, "ytmusic")      ||
        StringContains(aumid, "music.youtube")) {
        return Source::YTMusic;
    }
    // 网页端:SMTC 的 AUMID 是浏览器本身,只能靠标题尾巴识别。
    if (IsBrowserAumid(aumid) && LooksLikeYtMusicTitle(title)) {
        return Source::YTMusic;
    }
    return Source::Other;
}

// 去掉标题里的播放器后缀,并尽量拆出 "歌名 - 艺人" 形式。
// 浏览器里 YT Music 经常把 title 写成 "Song - Artist - YouTube Music",
// 而 Artist 字段为空 —— 不拆的话 LRCLib 很难命中。
static void CleanMetaForSource(Track& t) {
    if (t.source != Source::YTMusic) return;

    // 1) 剥后缀
    static const char* kSuffixes[] = {
        " - YouTube Music", " · YouTube Music", " • YouTube Music",
        " | YouTube Music", " — YouTube Music", " – YouTube Music",
        " - Youtube Music", " · Youtube Music",
    };
    for (const char* s : kSuffixes) {
        size_t n = std::strlen(s);
        if (t.title.size() > n &&
            _strnicmp(t.title.data() + t.title.size() - n, s, (unsigned)n) == 0) {
            t.title.resize(t.title.size() - n);
            // 去尾部空白
            while (!t.title.empty() &&
                   (t.title.back() == ' ' || t.title.back() == '\t'))
                t.title.pop_back();
            break;
        }
    }

    // 2) 若 artist 空,尝试从 "Song - Artist" / "Song • Artist" 拆
    if (t.artist.empty()) {
        // 从右往左找最后一个分隔,更接近 "Title - Artist" 习惯
        auto try_split = [&](const char* sep) -> bool {
            size_t n = std::strlen(sep);
            size_t pos = t.title.rfind(sep);
            if (pos == std::string::npos || pos == 0) return false;
            std::string left  = t.title.substr(0, pos);
            std::string right = t.title.substr(pos + n);
            // trim
            while (!left.empty()  && (left.back()  == ' ' || left.back()  == '\t')) left.pop_back();
            while (!right.empty() && (right.front()== ' ' || right.front()== '\t')) right.erase(right.begin());
            if (left.empty() || right.empty()) return false;
            // 右侧太长多半是 "Artist - Topic - Album" 之类,放弃
            if (right.size() > 80) return false;
            t.title  = left;
            t.artist = right;
            return true;
        };
        if (!try_split(" - ") && !try_split(" – ") && !try_split(" — ") &&
            !try_split(" • ") && !try_split(" · ") && !try_split(" | ")) {
            // 拆不动就算了,LRCLib 还能只靠 title 搜
        }
    }

    // 3) artist 里偶发 "Artist - Topic"(YT 自动频道),去掉尾巴提升匹配率
    {
        static const char* kArtistJunk[] = { " - Topic", " – Topic", " — Topic" };
        for (const char* s : kArtistJunk) {
            size_t n = std::strlen(s);
            if (t.artist.size() > n &&
                _strnicmp(t.artist.data() + t.artist.size() - n, s, (unsigned)n) == 0) {
                t.artist.resize(t.artist.size() - n);
                while (!t.artist.empty() &&
                       (t.artist.back() == ' ' || t.artist.back() == '\t'))
                    t.artist.pop_back();
                break;
            }
        }
    }
}

static std::string BuildMatchKey(const Track& t) {
    if (t.source == Source::NetEase && !t.ncm_id.empty()) {
        return "ncm:" + t.ncm_id;
    }
    if (t.title.empty() && t.artist.empty()) return {};
    std::string key = "meta:";
    key += t.title;     key += '|';
    key += t.artist;    key += '|';
    key += t.album;     key += '|';
    int dur = (int)(t.duration_ms / 1000);
    char buf[16]; std::snprintf(buf, sizeof(buf), "%d", dur);
    key += buf;
    return key;
}

// 选 session 的策略:
//   1) 优先 NCM-(网易云 inflink-rs)
//   2) 其次 Spotify / YT Music 桌面端 / 浏览器里标题带 YouTube Music 的会话
//   3) 最后才用系统 current session 兜底
// 浏览器同时开很多标签时,GetCurrentSession 经常不是音乐页,必须主动挑。
static winrt_ns::GlobalSystemMediaTransportControlsSession PickSession(
    winrt_ns::GlobalSystemMediaTransportControlsSessionManager const& mgr,
    std::string& out_ncm_id) {
    out_ncm_id.clear();
    auto sessions = mgr.GetSessions();

    winrt_ns::GlobalSystemMediaTransportControlsSession music_fallback{ nullptr };

    for (auto const& sess : sessions) {
        try {
            auto props = sess.TryGetMediaPropertiesAsync().get();
            if (!props) continue;

            // 1) NetEase via inflink genre tag
            try {
                auto genres = props.Genres();
                if (genres) {
                    for (auto const& g : genres) {
                        std::string s = Utf8FromHstring(g);
                        if (s.rfind("NCM-", 0) == 0) {
                            out_ncm_id = s.substr(4);
                            return sess;
                        }
                    }
                }
            } catch (...) {}

            // 2) Prefer known music sources over random browser tabs / videos.
            std::string aumid, title;
            try { aumid = Utf8FromHstring(sess.SourceAppUserModelId()); } catch (...) {}
            try { title = Utf8FromHstring(props.Title()); } catch (...) {}
            Source src = ClassifySource(aumid, title);
            if (src == Source::Spotify || src == Source::YTMusic) {
                // 正在播放的优先;暂停的也先记下来当 fallback
                try {
                    auto pb = sess.GetPlaybackInfo();
                    using PS = winrt_ns::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
                    if (pb && pb.PlaybackStatus() == PS::Playing) return sess;
                } catch (...) {}
                if (!music_fallback) music_fallback = sess;
            }
        } catch (...) {}
    }

    if (music_fallback) return music_fallback;

    try {
        return mgr.GetCurrentSession();
    } catch (...) {
        return nullptr;
    }
}

bool SmtcWatcher::Start() {
    if (m_running.exchange(true)) return false;
    m_thread = std::thread(&SmtcWatcher::ThreadProc, this);
    return true;
}

void SmtcWatcher::Stop() {
    // 只发停止信号并 join。绝不能在这里做 WinRT 调用 —— 退出路径上主线程
    // 若再阻塞在某个 SMTC async.get() 上,窗口会直接"未响应"。
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
}

void SmtcWatcher::ThreadProc() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    winrt_ns::GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    try {
        mgr = winrt_ns::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {
        m_running = false;
        return;
    }

    while (m_running.load()) {
        try {
            // 退出中尽快跳出,别再开一轮 SMTC 查询(那些 .get() 可能卡很久)。
            if (!m_running.load()) break;

            std::string ncm_id;
            auto sess = PickSession(mgr, ncm_id);
            if (!m_running.load()) break;
            if (sess) {
                auto track = std::make_shared<Track>();
                track->ncm_id = ncm_id;

                try { track->source_app = Utf8FromHstring(sess.SourceAppUserModelId()); } catch (...) {}

                auto props = sess.TryGetMediaPropertiesAsync().get();
                if (!m_running.load()) break;
                if (props) {
                    track->title  = Utf8FromHstring(props.Title());
                    track->artist = Utf8FromHstring(props.Artist());
                    track->album  = Utf8FromHstring(props.AlbumTitle());
                    if (track->ncm_id.empty()) {
                        auto genres = props.Genres();
                        if (genres) {
                            for (auto const& g : genres) {
                                std::string s = Utf8FromHstring(g);
                                if (s.rfind("NCM-", 0) == 0) { track->ncm_id = s.substr(4); break; }
                            }
                        }
                    }
                    // 同步读出专辑封面字节(JPG/PNG),交给主线程解码 + 上传 GPU。
                    // 退出时跳过封面读取 —— OpenReadAsync/LoadAsync 最容易卡住。
                    if (m_running.load()) try {
                        auto thumb_ref = props.Thumbnail();
                        if (thumb_ref) {
                            auto stream = thumb_ref.OpenReadAsync().get();
                            if (!m_running.load()) break;
                            uint32_t sz = (uint32_t)stream.Size();
                            if (sz > 0 && sz < 4 * 1024 * 1024) {
                                winrt::Windows::Storage::Streams::DataReader reader(stream);
                                reader.LoadAsync(sz).get();
                                if (!m_running.load()) break;
                                track->thumbnail_bytes.resize(sz);
                                reader.ReadBytes(winrt::array_view<uint8_t>(track->thumbnail_bytes));
                            }
                        }
                    } catch (...) {
                        track->thumbnail_bytes.clear();
                    }
                }

                auto pb = sess.GetPlaybackInfo();
                if (pb) {
                    auto st = pb.PlaybackStatus();
                    using PS = winrt_ns::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
                    if      (st == PS::Playing) track->status = Status::Playing;
                    else if (st == PS::Paused)  track->status = Status::Paused;
                    else                        track->status = Status::Stopped;
                    auto rate_ref = pb.PlaybackRate();
                    if (rate_ref) track->playback_rate = (float)rate_ref.Value();
                }

                // SMTC 的 Position 经常几秒才跳一次(尤其 Spotify / YT Music /
                // 浏览器标签页),不能当实时时钟。LastUpdatedTime 是媒体源报告
                // 这个 Position 的墙钟时刻 —— 用它做外推锚点,进度才会丝滑。
                // 若 LastUpdatedTime 缺失/离谱(时钟不同步),回退到采样时刻。
                auto tl = sess.GetTimelineProperties();
                int64_t raw_pos = 0;
                int64_t raw_dur = 0;
                int64_t last_upd_ms = 0; // FILETIME epoch ms, 0 = 不可用
                if (tl) {
                    raw_pos = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tl.Position()).count();
                    raw_dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tl.EndTime()).count();
                    try {
                        auto lu = tl.LastUpdatedTime();
                        // DateTime.UniversalTime 是 100ns ticks since 1601-01-01 UTC
                        // (FILETIME)。换算成 Unix ms 方便和 wall clock 对齐。
                        const int64_t kEpochDiff100ns = 116444736000000000LL;
                        int64_t ticks = lu.time_since_epoch().count();
                        if (ticks > kEpochDiff100ns) {
                            last_upd_ms = (ticks - kEpochDiff100ns) / 10000LL;
                        }
                    } catch (...) {
                        last_upd_ms = 0;
                    }
                }

                // 分类源 & 清理标题/艺人。NCM 始终是 NetEase,其它根据 aumid + title 判。
                // 网页 YT Music 会把 "Song - Artist - YouTube Music" 塞进 Title,
                // CleanMetaForSource 负责剥尾巴并拆 artist,LRCLib 才能命中。
                // 注意:match_key 依赖 title/artist,必须在粘滞逻辑之前算好。
                if (!track->ncm_id.empty()) {
                    track->source = Source::NetEase;
                } else {
                    track->source = ClassifySource(track->source_app, track->title);
                    CleanMetaForSource(*track);
                }
                track->match_key = BuildMatchKey(*track);

                // 浏览器网页端(YT Music 等)的 Timeline 经常卡死十几秒甚至更久:
                // Position 不变、LastUpdatedTime 也不变。若直接用 raw,外推到上限后
                // 歌词会冻在"十几秒前"。这里做粘滞外推:raw 没前进就继续本地走表,
                // 只有 raw 真正前进 / 大 seek / 暂停恢复时才重新锚定。
                const bool browserish =
                    (track->source == Source::YTMusic &&
                     IsBrowserAumid(ToLowerAscii(track->source_app))) ||
                    (track->source == Source::Other &&
                     IsBrowserAumid(ToLowerAscii(track->source_app)));
                track->sticky_timeline = browserish;

                int64_t sample_ms = NowMs();
                float rate = track->playback_rate;
                if (!(rate > 0.05f && rate < 4.f)) rate = 1.f;
                track->playback_rate = rate;

                const bool playing = (track->status == Status::Playing);
                const bool key_changed = (track->match_key != m_sticky_key);

                if (key_changed || !playing) {
                    // 切歌 / 暂停:直接采信 raw,重置粘滞状态
                    m_sticky_key = track->match_key;
                    m_sticky_raw_pos = raw_pos;
                    m_sticky_base_pos = raw_pos;
                    m_sticky_base_at_ms = sample_ms;
                    m_sticky_last_upd_ms = last_upd_ms;
                    m_sticky_rate = rate;
                    m_sticky_playing = playing;

                    // 非浏览器源仍可用 LastUpdatedTime 修锚点
                    if (!browserish && last_upd_ms > 0 && playing) {
                        FILETIME ft{};
                        GetSystemTimeAsFileTime(&ft);
                        ULARGE_INTEGER uli{};
                        uli.LowPart  = ft.dwLowDateTime;
                        uli.HighPart = ft.dwHighDateTime;
                        int64_t wall_now_ms = (int64_t)(uli.QuadPart / 10000ULL - 11644473600000ULL);
                        int64_t lag = wall_now_ms - last_upd_ms;
                        if (lag >= 0 && lag < 15000) {
                            m_sticky_base_at_ms = sample_ms - lag;
                        }
                    }

                    track->position_ms = m_sticky_base_pos;
                    track->position_sampled_at_ms = m_sticky_base_at_ms;
                } else if (browserish) {
                    // 浏览器粘滞路径
                    int64_t raw_delta = raw_pos - m_sticky_raw_pos;
                    bool last_upd_moved = (last_upd_ms > 0 &&
                                           m_sticky_last_upd_ms > 0 &&
                                           last_upd_ms != m_sticky_last_upd_ms);

                    if (raw_delta >= 400 || raw_delta <= -1500 || last_upd_moved) {
                        // raw 真正前进 / 后退(seek) / LastUpdatedTime 刷新 → 重新锚定
                        // 但若 raw 只是小回跳(<1.5s)且 last_upd 没动,忽略(浏览器抖动)
                        if (raw_delta <= -1500 || raw_delta >= 400 || last_upd_moved) {
                            // 若 raw 前进量明显小于本地已外推量,说明浏览器在补旧包,
                            // 取两者较大值,避免进度被拉回去。
                            int64_t local_now = m_sticky_base_pos;
                            if (m_sticky_playing) {
                                int64_t el = sample_ms - m_sticky_base_at_ms;
                                if (el < 0) el = 0;
                                local_now = m_sticky_base_pos + (int64_t)(el * (double)m_sticky_rate);
                            }
                            int64_t accept = raw_pos;
                            if (raw_delta > 0 && local_now > raw_pos + 800) {
                                // 本地已经跑到 raw 前面较多:平滑追,不要硬跳回
                                accept = local_now;
                            }
                            m_sticky_raw_pos = raw_pos;
                            m_sticky_base_pos = accept;
                            m_sticky_base_at_ms = sample_ms;
                            if (last_upd_ms > 0) m_sticky_last_upd_ms = last_upd_ms;
                            m_sticky_rate = rate;
                        }
                    }
                    // else: raw 冻住 → 继续用旧 base 外推,什么都不改

                    m_sticky_playing = true;
                    track->position_ms = m_sticky_base_pos;
                    track->position_sampled_at_ms = m_sticky_base_at_ms;
                    track->playback_rate = m_sticky_rate;
                } else {
                    // 桌面端 Spotify / YTMD / 网易云:LastUpdatedTime 锚点 + 直接采信 raw
                    m_sticky_key = track->match_key;
                    m_sticky_raw_pos = raw_pos;
                    m_sticky_base_pos = raw_pos;
                    m_sticky_base_at_ms = sample_ms;
                    m_sticky_last_upd_ms = last_upd_ms;
                    m_sticky_rate = rate;
                    m_sticky_playing = true;

                    if (last_upd_ms > 0) {
                        FILETIME ft{};
                        GetSystemTimeAsFileTime(&ft);
                        ULARGE_INTEGER uli{};
                        uli.LowPart  = ft.dwLowDateTime;
                        uli.HighPart = ft.dwHighDateTime;
                        int64_t wall_now_ms = (int64_t)(uli.QuadPart / 10000ULL - 11644473600000ULL);
                        int64_t lag = wall_now_ms - last_upd_ms;
                        if (lag >= 0 && lag < 15000) {
                            m_sticky_base_at_ms = sample_ms - lag;
                        }
                    }
                    track->position_ms = m_sticky_base_pos;
                    track->position_sampled_at_ms = m_sticky_base_at_ms;
                }

                track->duration_ms = raw_dur;
                m_current.store(track);
            } else {
                m_current.store(std::shared_ptr<Track>{});
            }
        } catch (...) {
            // WinRT 偶发异常吞掉,下一轮再来。
        }

        // 250ms 一轮。Position 本身更新很慢,轮询再密也没用;外推靠主线程
        // EffectivePositionMs,这里只要及时拿到新 Position / LastUpdatedTime。
        // sleep 拆成小段,Stop() 置位后最多 ~50ms 就能醒过来 join。
        for (int i = 0; i < 5 && m_running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 清掉当前 track,避免主线程退出后再读到悬挂 shared_ptr 内容
    // (shared_ptr 本身安全,但能让 UI 状态立刻变"无曲目")。
    m_current.store(std::shared_ptr<Track>{});
    try { winrt::uninit_apartment(); } catch (...) {}
}

}
