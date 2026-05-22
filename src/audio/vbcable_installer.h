#pragma once
#include <functional>
#include <string>

namespace audio {

enum class InstallStep {
    Idle = -1,
    Downloading = 0,
    Extracting = 1,
    LaunchingInstaller = 2,
    AwaitingUser = 3,
    Verifying = 4,
    Done = 5,
    Failed = 6,
};

struct InstallProgress {
    InstallStep step = InstallStep::Idle;
    float       fraction = 0.f;   // 0..1, only meaningful for Downloading
    std::string message;          // utf-8, suitable for UI display
};

namespace vbcable {

// True if VB-Cable's "CABLE Input" render endpoint is present.
bool IsInstalled();

// Synchronous; intended to be called from a detached worker thread. Progress
// callbacks may be invoked from this same thread before Install returns.
void Install(std::function<void(const InstallProgress&)> on_progress);

} // namespace vbcable
} // namespace audio
