#pragma once
#include <algorithm>
#include <cmath>
// Independent camera-relative movement; reference behavior studied in
// aikatore/recvx-enhanced-port (MIT), pc/src/input/modern.c and modern_glue.c.
namespace cv_modern_math {
constexpr float units=65536.f/6.283185307179586f;
inline float wrap(float a){return std::remainder(a,65536.f);}
inline float target(float rx,float rz,float x,float y){
 const float n=std::hypot(rx,rz);if(n<0.0001f)return 0;
 rx/=n;rz/=n;return std::atan2(-(x*rx+y*rz),-(x*rz-y*rx))*units;
}
struct Hold {
 bool active=false;unsigned room=0,cut=0;float rx=1,rz=0,reference=0;
 void reset(){active=false;}
 void use(unsigned new_room,unsigned new_cut,float x,float y,float& right_x,float& right_z){
  const float angle=std::atan2(x,y)*units;
  if(!active||room!=new_room||cut==new_cut||std::fabs(wrap(angle-reference))>65536.f*35/360){
   active=true;room=new_room;cut=new_cut;rx=right_x;rz=right_z;reference=angle;
  }else{right_x=rx;right_z=rz;}
 }
};
inline float turn(float current,float wanted){return std::clamp(wrap(wanted-current)*.4f,-65536.f/12,65536.f/12);}
inline bool stick(float& x,float& y){
 float n=std::hypot(x,y);if(n<=.24f){x=y=0;return false;}
 if(n>1){x/=n;y/=n;}return true;
}
}
