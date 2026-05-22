#pragma once
#include <optional>
#include <string>
#include <cstdint>

namespace audio {

struct NeteaseLocation {
    uint32_t    root_pid;
    std::string exe_name; // utf-8, lowercased, no extension
};

// Walk the process snapshot; return the root cloudmusic.exe / Orpheus.exe PID
// suitable to pass to ActivateAudioInterfaceAsync with
// PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE. CEF render children point
// back to the main process, so "the candidate whose parent is NOT also a
// Netease exe" is the root.
std::optional<NeteaseLocation> FindNeteaseRoot();

bool IsProcessAlive(uint32_t pid);

} // namespace audio
