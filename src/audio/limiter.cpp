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

    // 15% headroom 走 tanh 渐进 —— 信号永远逼近但触不到 ceiling,
    // 没有硬切产生的奇次谐波(Opus 在低码率下会把这些谐波放大成"金属感")。
    // 线性区(|x| <= knee)完全透传。
    const float ceiling = ceiling_;
    const float knee = ceiling * 0.85f;
    const float headroom = ceiling - knee;  // 软压缩可用的范围

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
            float excess  = ax - knee;
            // tanh 在 0 处斜率=1 跟 knee 处线性区连续,在 +inf 趋近 1,
            // 所以 knee + headroom * tanh(...) 永远不超过 ceiling。
            float shaped = knee + headroom * std::tanh(excess / headroom);
            samples[i] = sign * shaped;
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
