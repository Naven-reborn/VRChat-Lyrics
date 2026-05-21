#include "winhttp_client.h"

#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace net {

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), r.data(), n);
    return r;
}

static bool ParseUrl(const std::string& url, bool& secure, std::wstring& host,
                     int& port, std::wstring& path) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength    = (DWORD)-1;
    uc.dwHostNameLength  = (DWORD)-1;
    uc.dwUrlPathLength   = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;

    std::wstring wurl = Utf8ToWide(url);
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return false;

    secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    port = uc.nPort ? uc.nPort : (secure ? 443 : 80);
    path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    return true;
}

bool HttpGet(const std::string& url,
             const std::string& headers,
             std::string& out_body,
             int& out_status) {
    out_body.clear();
    out_status = 0;

    bool secure = false;
    std::wstring host, path;
    int port = 0;
    if (!ParseUrl(url, secure, host, port, path)) return false;

    HINTERNET hSession = WinHttpOpen(L"vrc-lyrics/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // resolve / connect / send / receive timeouts in ms.
    WinHttpSetTimeouts(hSession, 4000, 4000, 4000, 6000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    std::wstring whdr = Utf8ToWide(headers);
    BOOL sent = WinHttpSendRequest(hRequest,
        whdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : whdr.c_str(),
        (DWORD)(whdr.empty() ? 0 : -1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (sent) sent = WinHttpReceiveResponse(hRequest, nullptr);

    bool ok = false;
    if (sent) {
        DWORD code = 0, code_size = sizeof(code);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size, WINHTTP_NO_HEADER_INDEX);
        out_status = (int)code;

        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
            if (avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            out_body.append(buf.data(), read);
        } while (avail > 0);

        ok = (code >= 200 && code < 300);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

}
