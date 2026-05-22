#include "limiter.h"
#include <cmath>
#include <algorithm>

namespace audio {

float DbToLinear(float db) {
    if (db <= -120.f) return 0.f;
    return std::pow(10.f, db / 20.f);
}

void ApplyGain(float* samples, uint32_t frame_count, int channels, float linear_gain) {
    if (linear_gain == 1.f) return;
    uint32_t n = frame_count * (uint32_t)channels;
    for (uint32_t i = 0; i < n; ++i) samples[i] *= linear_gain;
}

void SoftLimiter::Configure(float ceiling_dbfs, bool enabled) {
    ceiling_ = DbToLinear(ceiling_dbfs);
    enabled_ = enabled;
}

void SoftLimiter::Process(float* samples, uint32_t frame_count, int channels) {
    uint32_t n = frame_count * (uint32_t)channels;
    float peak = 0.f;

    // Knee: only the top 5% of headroom uses a soft curve; everything below
    // passes through completely untouched. Above ceiling, hard clip.
    const float knee = ceiling_ * 0.95f;
    const float knee_range = ceiling_ - knee;
    const float ceiling = ceiling_;

    if (enabled_) {
        for (uint32_t i = 0; i < n; ++i) {
            float x  = samples[i];
            float ax = std::fabs(x);
            if (ax > peak) peak = ax;

            if (ax <= knee) {
                // Linear region — fully transparent.
                continue;
            }
            float sign = (x >= 0.f) ? 1.f : -1.f;
            if (ax >= ceiling) {
                // Hard clip at ceiling.
                samples[i] = sign * ceiling;
            } else {
                // Soft knee: quadratic that's tangent to y=x at knee and
                // tangent to y=ceiling at ceiling. ~5% headroom of curving.
                float t = (ax - knee) / knee_range;       // 0..1
                float shaped = knee + knee_range * (t - 0.5f * t * t);
                samples[i] = sign * shaped;
            }
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            float ax = std::fabs(samples[i]);
            if (ax > peak) peak = ax;
        }
    }
    last_peak_dbfs_ = (peak > 1e-6f)
        ? 20.f * std::log10(peak)
        : -120.f;
}

} // namespace audio
