#pragma once
#include <string>

namespace net {

// Synchronous HTTP/HTTPS GET via WinHTTP.
// Headers is a "key: value\r\nkey: value\r\n..." string (may be empty).
// Returns true if status code is 2xx and body was read.
bool HttpGet(const std::string& url,
             const std::string& headers,
             std::string& out_body,
             int& out_status);

// Single-hop GET with redirects disabled. If the server responds with 3xx,
// out_location is filled with the Location header. Body is discarded.
// Used to resolve b23.tv short links without downloading the destination page.
bool ResolveRedirect(const std::string& url,
                     const std::string& headers,
                     std::string& out_location,
                     int& out_status);

// Synchronous HTTP/HTTPS POST via WinHTTP.
// Headers is a "key: value\r\n..." string (Content-Type 必须由调用方在 headers 里指定)。
// body 是要发的原始字节,通常是 UTF-8 JSON。timeout_ms 是接收超时,LLM 推理可能慢,默认放宽到 60s。
// Returns true 当 status 2xx,out_body 始终填充(失败时能拿到错误 JSON)。
bool HttpPost(const std::string& url,
              const std::string& headers,
              const std::string& body,
              std::string& out_body,
              int& out_status,
              int timeout_ms = 60000);

}
