#pragma once
#include <string>
#include <chrono>

namespace osc {

class Chatbox {
public:
    bool Init();           // creates UDP socket, WSAStartup
    void Shutdown();

    void SetTarget(const char* host, int port);
    void SetRateLimitMs(int ms) { m_rate_limit_ms = ms; }
    // VRChat chatbox 无更新约 ~20–30s 会自动消失。相同文案超过这个间隔
    // 会强制再发一次(keep-alive),默认 12s,足够压过超时又不会刷屏。
    void SetKeepAliveMs(int ms) { m_keepalive_ms = ms > 0 ? ms : 12000; }

    // Throttled send. Returns true if the message was actually transmitted,
    // false if dropped by the rate limiter or no socket.
    // 相同文案:超过 keep-alive 间隔仍会重发,避免暂停/静态状态时气泡被 VRChat 清掉。
    bool TrySend(const std::string& text);

    // Bypass rate limiter (e.g. on stop, send empty string immediately).
    bool ForceSend(const std::string& text);

private:
    bool SendRaw(const std::string& text);

    using clock = std::chrono::steady_clock;

    void*       m_sock = nullptr;        // SOCKET (cast at use site)
    bool        m_wsa_inited = false;
    std::string m_host = "127.0.0.1";
    int         m_port = 9000;
    int         m_rate_limit_ms = 1300;
    int         m_keepalive_ms = 12000;  // re-send identical text before VRC timeout
    clock::time_point m_last_send{};
    std::string m_last_text;
};

}
