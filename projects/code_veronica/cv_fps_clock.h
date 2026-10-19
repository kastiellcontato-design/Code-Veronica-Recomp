#pragma once
#include <algorithm>
#include <cmath>
namespace cv_fps {
// Consume the engine's elapsed time in native 30 Hz ticks. Never alternate
// blindly: 35 rendered frames/s still need about 30 logic updates/s.
class LogicClock {
    double pending_=0;bool first_=true;
public:
    void reset() {pending_=0;first_=true;}
    unsigned advance(double ticks) {
        if(!std::isfinite(ticks)||ticks<0)return 0;
        if(first_) {first_=false;return 1;}
        // Bound catch-up after a stall. No unbounded burst of guest updates.
        pending_+=std::min(ticks,4.0);
        unsigned count=static_cast<unsigned>(std::floor(pending_+1e-9));
        pending_=std::max(0.0,pending_-count);
        return std::min(count,4u);
    }
};
}
