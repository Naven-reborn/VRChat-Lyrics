#include "vbcable_installer.h"
#include "devices.h"
#include "net/winhttp_client.h"

#include <Windows.h>
#include <shellapi.h>
#include <combaseapi.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#pragma comment(lib, "shell32.lib")

namespace audio::vbcable {

namespace {

// VB-Audio's stable download URL. Pack43 is the current shipping version as
// of late 2025. If they bump the version number, the website redirects, and
// WinHTTP follows 3xx by default, so we should keep working without code
// changes.
constexpr const char* kDownloadUrl =
    "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack43.zip";

std::filesystem::path TempRoot() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0) return std::filesystem::current_path();
    return std::filesystem::path(buf, buf + n) / L"vrc-lyrics-vbcable";
}

bool RunHidden(const std::wstring& cmdline, DWORD timeout_ms, DWORD& exit_code) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutable_cmd = cmdline; // CreateProcessW writes to it
    BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    DWORD w = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        exit_code = (DWORD)-1;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::wstring SysPath(const wchar_t* exe) {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0) return exe;
    return std::wstring(sys, sys + n) + L"\\" + exe;
}

void Report(const std::function<void(const InstallProgress&)>& cb,
            InstallStep step, float frac, std::string msg) {
    if (!cb) return;
    InstallProgress p{ step, frac, std::move(msg) };
    cb(p);
}

} // namespace

bool IsInstalled() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool fresh = SUCCEEDED(hr);
    bool found = FindVbCable().has_value();
    if (fresh) CoUninitialize();
    return found;
}

void Install(std::function<void(const InstallProgress&)> on_progress) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_fresh = SUCCEEDED(hr);

    auto fail = [&](const std::string& msg) {
        Report(on_progress, InstallStep::Failed, 0.f, msg);
        if (com_fresh) CoUninitialize();
    };

    auto root = TempRoot();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    auto zip_path     = root / L"driver_pack.zip";
    auto extract_dir  = root / L"extracted";
    std::filesystem::create_directories(extract_dir, ec);

    // Step 1: download.
    Report(on_progress, InstallStep::Downloading, 0.05f, "Downloading VB-Cable...");
    std::string body;
    int status = 0;
    bool downloaded = net::HttpGet(kDownloadUrl, "", body, status);
    if (!downloaded && (status == 0 || status >= 500)) {
        // One retry — the 6s WinHTTP timeout can bite on slower links.
        Report(on_progress, InstallStep::Downloading, 0.1f, "Retrying download...");
        body.clear();
        downloaded = net::HttpGet(kDownloadUrl, "", body, status);
    }
    if (!downloaded || body.size() < 100 * 1024) {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "Download failed (status=%d, size=%zu)", status, body.size());
        fail(buf);
        return;
    }
    Report(on_progress, InstallStep::Downloading, 0.95f, "Saving installer...");
    {
        std::ofstream out(zip_path, std::ios::binary);
        if (!out) { fail("Cannot write zip"); return; }
        out.write(body.data(), (std::streamsize)body.size());
    }

    // Step 2: extract with bundled tar.exe.
    Report(on_progress, InstallStep::Extracting, 0.0f, "Extracting...");
    std::wstring tar = SysPath(L"tar.exe");
    std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip_path.wstring()
                       + L"\" -C \"" + extract_dir.wstring() + L"\"";
    DWORD code = 0;
    if (!RunHidden(cmd, 30000, code) || code != 0) {
        char buf[128];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Extract failed (code=%lu)", code);
        fail(buf);
        return;
    }

    // Step 3: find VBCABLE_Setup_x64.exe in the extracted tree.
    std::filesystem::path setup;
    for (auto& e : std::filesystem::recursive_directory_iterator(extract_dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        auto name = e.path().filename().wstring();
        if (_wcsicmp(name.c_str(), L"VBCABLE_Setup_x64.exe") == 0) {
            setup = e.path();
            break;
        }
    }
    if (setup.empty()) {
        // Fallback: 32-bit installer.
        for (auto& e : std::filesystem::recursive_directory_iterator(extract_dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().wstring();
            if (_wcsicmp(name.c_str(), L"VBCABLE_Setup.exe") == 0) {
                setup = e.path();
                break;
            }
        }
    }
    if (setup.empty()) { fail("Installer exe not found in package"); return; }

    // Step 4: launch installer with elevation (UAC).
    Report(on_progress, InstallStep::LaunchingInstaller, 0.f,
           "Launching installer (UAC prompt)...");
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = setup.c_str();
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        DWORD le = GetLastError();
        if (le == ERROR_CANCELLED) {
            fail("User cancelled UAC prompt");
        } else {
            char buf[128];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                        "ShellExecuteEx failed (err=%lu)", le);
            fail(buf);
        }
        return;
    }

    // Step 5: wait for installer process to exit (user clicks through wizard).
    Report(on_progress, InstallStep::AwaitingUser, 0.f,
           "Click 'Install Driver' in the VB-Cable wizard...");
    if (sei.hProcess) {
        // 5-minute upper bound.
        WaitForSingleObject(sei.hProcess, 5 * 60 * 1000);
        CloseHandle(sei.hProcess);
    }

    // Step 6: poll for "CABLE Input" endpoint to appear.
    Report(on_progress, InstallStep::Verifying, 0.f, "Verifying driver registration...");
    bool found = false;
    for (int i = 0; i < 120; ++i) { // up to 60s
        if (FindVbCable().has_value()) { found = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!found) {
        fail("Driver not detected — a reboot may be required");
        return;
    }

    Report(on_progress, InstallStep::Done, 1.f, "VB-Cable installed");
    if (com_fresh) CoUninitialize();
}

} // namespace audio::vbcable
