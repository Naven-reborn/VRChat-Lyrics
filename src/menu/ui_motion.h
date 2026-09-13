#pragma once
#include <algorithm>
#include <cmath>

namespace menu::motion {
inline float Ease(float t) {
    const float u=1.f-std::clamp(t,0.f,1.f);
    return 1.f-u*u*u;
}
// Finite, monotonic, frame-rate-independent motion. Retarget from the currently
// rendered value on reversal, with no overshoot, impulse or expanding geometry.
struct Tween {
    float value=0, from=0, target=0, elapsed=1;
    void Reset(float v) { value=from=target=v; elapsed=1; }
    float Tick(float goal,float dt,float duration,bool enabled=true) {
        if (!enabled || duration<=0) { Reset(goal); return value; }
        if (goal!=target) { from=value; target=goal; elapsed=0; }
        elapsed=std::min(duration,elapsed+std::clamp(dt,0.f,.25f));
        value=from+(target-from)*Ease(elapsed/duration);
        if (elapsed>=duration) value=target;
        return value;
    }
};

// Approved A: 2.5% compression, soft inset shading, 320ms gentle rebound.
// Stable layout/hitbox: only the button's rendered vertices are scaled.
struct SoftPress {
    float scale=1.f, from=1.f, elapsed=.32f, held_time=0.f;
    bool held=false, quick=false;
    float Tick(bool down,float dt,bool enabled=true) {
        dt=std::clamp(dt,0.f,.10f);
        if(!enabled) { held=down;scale=from=1.f;elapsed=.32f;held_time=0;return scale; }
        if(down!=held) {
            from=scale;elapsed=0;
            quick=!down && held_time<.07f;
            held=down;
            if(down)held_time=0;
        }
        if(held)held_time+=dt;
        elapsed+=dt;
        if(held) scale=from+(.975f-from)*Ease(elapsed/.10f);
        else {
            const float t=std::min(elapsed/.32f,1.f);
            const float a=quick?.16f:0.f;
            if(quick && t<a)scale=from+(.975f-from)*Ease(t/a);
            else if(t<.57f){const float start=quick?.975f:from;scale=start+(1.006f-start)*Ease((t-a)/(.57f-a));}
            else if(t<.82f)scale=1.006f+(.999f-1.006f)*Ease((t-.57f)/.25f);
            else scale=.999f+.001f*Ease((t-.82f)/.18f);
            if(t>=1.f)scale=1.f;
        }
        return scale;
    }
};
}
