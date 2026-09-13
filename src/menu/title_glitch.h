#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace menu::motion {
struct Tear {
    float y=0, height=0, x=0, width=0, shift=0;
};
struct GlitchFrame {
    float strength=0, jitter=0;
    int count=0;
    std::array<Tear,2> tears{};
};
inline uint32_t GlitchHash(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
inline float GlitchRandom(uint32_t& seed) {
    seed=GlitchHash(seed+0x9e3779b9u);return (seed&0xffffffu)/16777215.f;
}
inline float SmoothUnit(float t) {t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}

// Three-second cycle: one second active, two seconds clean. Last 350ms of
// the active second restore the same frozen pattern; no random jumps on exit.
inline GlitchFrame TitleGlitch(double seconds,uint32_t seed,bool enabled=true) {
    GlitchFrame out;
    if(!enabled || !std::isfinite(seconds) || seconds<0)return out;
    const double cycle=std::floor(seconds/3.0);
    const float phase=(float)(seconds-cycle*3.0);
    if(phase>=1.f)return out;
    out.strength=SmoothUnit(phase/.08f)*(1.f-SmoothUnit((phase-.65f)/.35f));
    if(out.strength<=.00001f){out.strength=0;return out;}
    uint32_t rng=GlitchHash(seed^(uint32_t)std::fmod(cycle,4294967295.0));
    const float sample=std::min(phase,.65f);
    float boundary=0;
    // Irregularly held patterns (110..230ms), not rows tied to a fixed grid.
    for(int i=0;i<8;++i){
        const uint32_t previous=rng;
        boundary+=.11f+.12f*GlitchRandom(rng);
        if(boundary>sample){rng=previous;break;}
    }
    out.jitter=(GlitchRandom(rng)-.5f)*.6f;
    const float roll=GlitchRandom(rng);
    out.count=roll<.18f?0:roll<.84f?1:2;
    for(int i=0;i<out.count;++i){
        auto& tear=out.tears[i];
        tear.y=.18f+.59f*GlitchRandom(rng);
        if(i && std::fabs(tear.y-out.tears[0].y)<.12f)
            tear.y=out.tears[0].y>.47f?out.tears[0].y-.21f:out.tears[0].y+.21f;
        tear.height=.035f+.025f*GlitchRandom(rng);
        tear.x=.05f+.45f*GlitchRandom(rng);
        tear.width=.16f+.34f*GlitchRandom(rng);
        tear.shift=(GlitchRandom(rng)<.5f?-1.f:1.f)*(.06f+.08f*GlitchRandom(rng));
    }
    if(out.count==2 && out.tears[0].y>out.tears[1].y)std::swap(out.tears[0],out.tears[1]);
    return out;
}
}
