#include "smtc.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
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

// 选 session 的策略:优先挑 Genres 里带 NCM- 前缀的(那就是网易云,inflink-rs
// 插件会写进去);找不到就用系统当前 session 兜底。
static winrt_ns::GlobalSystemMediaTransportControlsSession PickSession(
    winrt_ns::GlobalSystemMediaTransportControlsSessionManager const& mgr,
    std::string& out_ncm_id) {
    out_ncm_id.clear();
    auto sessions = mgr.GetSessions();
    for (auto const& sess : sessions) {
        try {
            auto props = sess.TryGetMediaPropertiesAsync().get();
            if (!props) continue;
            auto genres = props.Genres();
            if (!genres) continue;
            for (auto const& g : genres) {
                std::string s = Utf8FromHstring(g);
                if (s.rfind("NCM-", 0) == 0) {
                    out_ncm_id = s.substr(4);
                    return sess;
                }
            }
        } catch (...) {}
    }
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
            std::string ncm_id;
            auto sess = PickSession(mgr, ncm_id);
            if (sess) {
                auto track = std::make_shared<Track>();
                track->ncm_id = ncm_id;

                try { track->source_app = Utf8FromHstring(sess.SourceAppUserModelId()); } catch (...) {}

                auto props = sess.TryGetMediaPropertiesAsync().get();
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
                    try {
                        auto thumb_ref = props.Thumbnail();
                        if (thumb_ref) {
                            auto stream = thumb_ref.OpenReadAsync().get();
                            uint32_t sz = (uint32_t)stream.Size();
                            if (sz > 0 && sz < 4 * 1024 * 1024) {
                                winrt::Windows::Storage::Streams::DataReader reader(stream);
                                reader.LoadAsync(sz).get();
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

                auto tl = sess.GetTimelineProperties();
                if (tl) {
                    track->position_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tl.Position()).count();
                    track->duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tl.EndTime()).count();
                }
                track->position_sampled_at_ms = NowMs();

                m_current.store(track);
            } else {
                m_current.store(std::shared_ptr<Track>{});
            }
        } catch (...) {
            // WinRT 偶发异常吞掉,下一轮再来。
        }

        // 200ms 一轮 —— 时间漂移在主线程那边用 steady_clock 补偿,这里粗糙点没事。
        for (int i = 0; i < 4 && m_running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    winrt::uninit_apartment();
}

}
