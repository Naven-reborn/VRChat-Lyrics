#include "process_find.h"

#include <Windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

namespace audio {

namespace {

bool IsNeteaseExe(const wchar_t* name) {
    // Case-insensitive compare against whitelist. PROCESSENTRY32W::szExeFile
    // contains the filename including .exe.
    static const wchar_t* kNames[] = { L"cloudmusic.exe", L"orpheus.exe" };
    for (auto* w : kNames) {
        if (_wcsicmp(name, w) == 0) return true;
    }
    return false;
}

std::string Utf8FromWide(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        r.data(), n, nullptr, nullptr);
    return r;
}

} // namespace

std::optional<NeteaseLocation> FindNeteaseRoot() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;

    struct Entry { DWORD pid; DWORD ppid; std::wstring name; };
    std::vector<Entry> all;
    all.reserve(512);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            all.push_back({ pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile });
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // First pass: candidates that match the whitelist.
    std::vector<Entry> candidates;
    for (auto& e : all) {
        if (IsNeteaseExe(e.name.c_str())) candidates.push_back(e);
    }
    if (candidates.empty()) return std::nullopt;

    // Build a PID set for fast "is this PID a netease process" check.
    auto is_netease_pid = [&](DWORD pid) {
        for (auto& c : candidates) if (c.pid == pid) return true;
        return false;
    };

    // Root = candidate whose parent is not in the netease set.
    std::vector<Entry> roots;
    for (auto& c : candidates) {
        if (!is_netease_pid(c.ppid)) roots.push_back(c);
    }

    // Fallback: if topology is weird (snapshot raced, parent gone),
    // treat the lowest-PID candidate as root.
    if (roots.empty()) roots = candidates;

    // If multiple instances, prefer earliest creation time.
    DWORD best_pid = roots.front().pid;
    std::wstring best_name = roots.front().name;
    ULONGLONG best_create = ULLONG_MAX;
    for (auto& r : roots) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid);
        if (!h) continue;
        FILETIME ct, et, kt, ut;
        if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
            ULARGE_INTEGER u; u.LowPart = ct.dwLowDateTime; u.HighPart = ct.dwHighDateTime;
            if (u.QuadPart < best_create) {
                best_create = u.QuadPart;
                best_pid = r.pid;
                best_name = r.name;
            }
        }
        CloseHandle(h);
    }

    // Lowercase + strip ".exe".
    std::wstring stem = best_name;
    for (auto& c : stem) c = (wchar_t)towlower(c);
    auto dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem.resize(dot);

    return NeteaseLocation{ (uint32_t)best_pid, Utf8FromWide(stem) };
}

bool IsProcessAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
}

} // namespace audio
