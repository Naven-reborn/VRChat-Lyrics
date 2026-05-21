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

    // Throttled send. Returns true if the message was actually transmitted,
    // false if dropped by the rate limiter or no socket.
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
    clock::time_point m_last_send{};
    std::string m_last_text;
};

}
