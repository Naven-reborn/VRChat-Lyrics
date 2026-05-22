#pragma once
#include <optional>
#include <string>
#include <vector>

namespace audio {

struct RenderDevice {
    std::wstring id;            // immutable endpoint id from MMDevice
    std::string  friendly_utf8; // friendly name for UI
    bool         is_default;
    bool         is_vbcable;    // friendly name contains "CABLE Input"
};

// Enumerate active render endpoints. COM must be initialized on the caller's
// thread (MTA recommended).
std::vector<RenderDevice> EnumRenderDevices();

// Find a VB-Cable input device. Returns the first match if multiple variants
// are installed.
std::optional<RenderDevice> FindVbCable();

bool DeviceExists(const std::wstring& id);

} // namespace audio
