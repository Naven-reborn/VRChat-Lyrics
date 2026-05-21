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

}
