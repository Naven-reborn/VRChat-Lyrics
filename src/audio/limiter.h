#pragma once
#include <cstdint>

namespace audio {

// Convert dB to a linear gain multiplier. -inf dB clamped to 0.
float DbToLinear(float db);

// In-place: multiply float32 samples by linear gain, no clipping protection.
void ApplyGain(float* samples, uint32_t frame_count, int channels, float linear_gain);

// Stateless soft limiter using a tanh-shape curve. Designed so that input
// magnitudes well below `ceiling_linear` pass through nearly unity and the
// curve asymptotes to ceiling. Cheap, no look-ahead, ~6 ops per sample.
class SoftLimiter {
public:
    void Configure(float ceiling_dbfs = -3.0f, bool enabled = true);
    void Process(float* samples, uint32_t frame_count, int channels);
    float LastPeakDbFs() const { return last_peak_dbfs_; }

private:
    float ceiling_     = 0.7079f;    // 10^(-3.0/20) — leaves 3 dB headroom for Opus
    bool  enabled_     = true;
    float last_peak_dbfs_ = -120.f;
};

} // namespace audio
