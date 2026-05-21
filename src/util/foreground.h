#pragma once
#include <string>

namespace util {

// Returns a short, human-friendly name of the foreground window's owning
// process (e.g. "VRChat", "chrome", "code"). Empty string if it's our own
// process or detection fails.
std::string ForegroundAppName();

}
