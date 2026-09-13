#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace menu {
// Row-timestamp animation only: never invent word-level timing.
struct LyricMotion {
    std::string key, current, outgoing;
    float elapsed = 1.f;
    int last_position = -1;
    bool initialized = false;
    static constexpr float duration = .55f;

    void Update(const std::string& identity, const std::string& text,
                int position, float dt, bool enabled) {
        const bool seek = last_position >= 0 &&
            (position < last_position - 800 || position > last_position + 2000);
        if (!initialized || identity != key || seek || !enabled) {
            key = identity; current = text; outgoing.clear(); elapsed = duration;
            initialized = true;
        } else if (text != current) {
            outgoing = current; current = text; elapsed = 0.f;
        }
        last_position = position;
        elapsed = std::min(duration, elapsed + std::clamp(dt, 0.f, .05f));
        if (elapsed >= duration) outgoing.clear();
    }
    float Progress() const { return std::clamp(elapsed / duration, 0.f, 1.f); }
    float Ease() const { const float u = 1.f - Progress(); return 1.f - u*u*u*u*u; }
};
}
