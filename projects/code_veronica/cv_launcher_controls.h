#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../../libs/input/pad_phys.h"
namespace cv_launcher_controls {
constexpr uint32_t srt_chord=(1u<<PB_L3)|(1u<<PB_R1)|(1u<<PB_EAST);
constexpr uint32_t menu_chord=(1u<<PB_L2)|(1u<<PB_R3)|(1u<<PB_R1);
constexpr bool chord_active(uint32_t buttons,uint32_t chord) {return (buttons&chord)==chord;}
inline int slider_value(float x,float left,float width,int minimum,int maximum) {
    if(width<=0||maximum<=minimum)return minimum;
    const double ratio=std::clamp((double(x)-left)/width,0.0,1.0);
    return minimum+int(std::lround(ratio*(maximum-minimum)));
}
inline float slider_fraction(int value,int minimum,int maximum) {
    return maximum>minimum?std::clamp(float(value-minimum)/float(maximum-minimum),0.f,1.f):0.f;
}
}
