#include "chatbox.h"
#include "osc_message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace osc {

bool Chatbox::Init() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    m_wsa_inited = true;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        m_wsa_inited = false;
        return false;
    }
    m_sock = (void*)s;
    return true;
}

void Chatbox::Shutdown() {
    if (m_sock) {
        closesocket((SOCKET)m_sock);
        m_sock = nullptr;
    }
    if (m_wsa_inited) {
        WSACleanup();
        m_wsa_inited = false;
    }
}

void Chatbox::SetTarget(const char* host, int port) {
    m_host = host;
    m_port = port;
}

bool Chatbox::SendRaw(const std::string& text) {
    if (!m_sock) return false;

    uint8_t buf[1200];
    int n = EncodeChatbox(text.c_str(), true, false, buf, sizeof(buf));
    if (n == 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);

    int sent = sendto((SOCKET)m_sock, (const char*)buf, n, 0,
                      (sockaddr*)&addr, sizeof(addr));
    if (sent == SOCKET_ERROR) return false;
    m_last_send = clock::now();
    m_last_text = text;
    return true;
}

bool Chatbox::TrySend(const std::string& text) {
    auto now = clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_send).count();

    // 相同文案:以前直接 drop,暂停后进度/歌词都不变 → 永远不再发 →
    // VRChat chatbox 大约 20–30s 无刷新就自己清掉,气泡消失。
    // 现在相同文案在 keep-alive 窗口内才 dedupe;过了窗口强制再刷一次。
    if (text == m_last_text) {
        if (elapsed < m_keepalive_ms) return false;
        return SendRaw(text);
    }

    if (elapsed < m_rate_limit_ms) return false;
    return SendRaw(text);
}

bool Chatbox::ForceSend(const std::string& text) {
    return SendRaw(text);
}

}
