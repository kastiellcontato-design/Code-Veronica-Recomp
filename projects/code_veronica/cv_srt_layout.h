#pragma once
#include <algorithm>
#include <cmath>
namespace cv_srt_layout {
inline float position(float extent,float panel,int normalized){return std::round(std::max(0.f,extent-panel)*std::clamp(normalized,0,1000)/1000.f);}
inline int normalized(float position,float extent,float panel){const float room=extent-panel;return room>0?std::clamp(int(std::lround(position*1000.f/room)),0,1000):0;}
}
