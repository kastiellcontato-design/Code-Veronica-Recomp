#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../../libs/input/pad_phys.h"
namespace cv_launcher_controls {
// Every in-game shortcut on a controller: R1+R2+L1+L2 held, plus one button,
// in the order of the keys F5..F10.
constexpr uint32_t shoulders=(1u<<PB_L1)|(1u<<PB_R1)|(1u<<PB_L2)|(1u<<PB_R2);
constexpr uint32_t menu_chord=shoulders|(1u<<PB_L3);       // F5: the options menu (or Insert)
constexpr uint32_t items_chord=shoulders|(1u<<PB_NORTH);   // F6: the item menu (triangle / Y)
constexpr uint32_t test_chord=shoulders|(1u<<PB_EAST);     // F7: the map window (circle / B)
constexpr uint32_t srt_chord=shoulders|(1u<<PB_SOUTH);     // F8: the SRT (cross / A)
constexpr uint32_t tex_chord=shoulders|(1u<<PB_WEST);      // F9: texture mods (square / X)
constexpr uint32_t shot_chord=shoulders|(1u<<PB_R3);       // F10: a screenshot
// A window of the port's ignores its own buttons while both triggers are
// held: they belong to one of the chords above.
constexpr bool chord_held(uint32_t buttons) {return (buttons&((1u<<PB_L2)|(1u<<PB_R2)))==((1u<<PB_L2)|(1u<<PB_R2));}
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
