#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "cv_srt.h"
namespace cv_combat_hud {
// Native inventory uses absolute HP (EU 0019EDA4, US 0019D34C, JP 0019D3F4),
// independently of the regional/difficulty maximum used to fill the ring.
inline int health_condition(int hp,int condition){
 if(hp<=0)return SRT_DEAD;
 if(condition==SRT_GAS||condition==SRT_POISON)return condition;
 return hp>=120?SRT_FINE:hp>=60?SRT_CAUTION:hp>=30?SRT_WARNING:SRT_DANGER;
}
struct Weapon {
 int id=0,loaded=0,reserve=0;bool infinite=false,percent=false,reserve_known=true;
 bool operator==(const Weapon&) const=default;
};
inline Weapon weapon(const cv_srt_state& s){
 if(s.equipped<1||s.equipped>CV_SRT_INVENTORY_SIZE)return {};
 const auto& gun=s.inventory[s.equipped-1];Weapon w{gun.id,gun.quantity,0,gun.infinite!=0,gun.id==2||gun.id==0x22||gun.id==0x8e};
 int ammo=0;
 switch(w.id){
 case 5:case 9:case 10:case 0x83:ammo=0xc;break;
 case 4:ammo=0xe;break;case 3:ammo=0x28;break;case 2:ammo=0x2a;break;
 case 0x22:ammo=0x26;break;case 0x8e:ammo=0x8f;break;
 case 0x20:ammo=0x1e;break;
 case 6:{const int kinds[]={0xf,0x10,0x11,0x25};ammo=kinds[(w.loaded>>6)&3];w.loaded&=63;break;}
 case 7:ammo=0x12;break;
 case 0x37:w.loaded=0;w.infinite=false;w.reserve_known=false;break; // Lighter: icon only.
 case 8:case 1:case 11:case 0x21:break;
 default:w={};return w;
 }
 for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++)if(i!=s.equipped-1&&ammo&&s.inventory[i].id==ammo)w.reserve+=s.inventory[i].quantity;
 // Some copies use the alternate native magnum-ammunition record.
 if(w.id==0x20)for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++)if(s.inventory[i].id==0xd)w.reserve+=s.inventory[i].quantity;
 return w;
}
inline float life_pulse(uint64_t now){return .90f+.10f*std::cos(float(now%1600)*(6.283185307f/1600.f));}
inline float resolution_scale(int index){const float heights[]={480,720,1080,1440,2160};return heights[std::clamp(index,0,4)]/1080.f;}
inline float life_sweep(float position,float fill,uint64_t now){
 if(fill<=0||position<0||position>fill)return 0;
 // A complete visible pass every 2.6 s, even with very little HP left.
 // Normalize to remaining life instead of leaving the band offscreen
 // during most of the HUD's five-second visibility window.
 const float centre=-.18f+1.36f*float(now%2600)/2600.f;
 const float d=(position/fill-centre)/.115f;
 return std::exp(-d*d);
}
struct Layout {float scale,x,y;};
inline Layout layout(uint32_t width,uint32_t height){
 const float scale=std::min(float(height)/1080.f,float(width)/1280.f);
 return {scale,float(width)-295*scale,float(height)-235*scale}; // 20px left; preserve the accepted vertical position.
}
struct Visibility {
 bool known=false;int character=-1,room=-1,weapon=-1,ammo=-1,hp=-1;uint64_t last=0;
 void reset(){*this={};}
 float update(bool enabled,bool valid,bool aiming,int next_character,int next_room,const Weapon& w,int health,uint64_t now){
  if(!enabled||!valid){reset();return 0;}
  bool action=!known||character!=next_character||room!=next_room||weapon!=w.id||ammo!=w.loaded||hp!=health||aiming;
  if(action)last=now;
  known=true;character=next_character;room=next_room;weapon=w.id;ammo=w.loaded;hp=health;
  const uint64_t age=now>=last?now-last:0;
  if(age<=5000)return 1;
  const float t=std::clamp(float(age-5000)/600.f,0.f,1.f);
  return 1-t*t*(3-2*t);
 }
};
struct HealthTrail {
 bool known=false;int character=-1,room=-1,maximum=0;float target=0,from=0;uint64_t started=0;
 void reset(){*this={};}
 float value(uint64_t now)const{
  const uint64_t age=now>=started?now-started:0;
  const float t=std::clamp((float(age)-120.f)/430.f,0.f,1.f);
  return target+(from-target)*(1-t*t*(3-2*t));
 }
 float update(bool valid,int next_character,int next_room,int hp,int maxhp,uint64_t now){
  if(!valid||maxhp<=0){reset();return 0;}
  const float next=std::clamp(float(hp)/maxhp,0.f,1.f);
  if(!known||character!=next_character||room!=next_room||maximum!=maxhp){known=true;from=target=next;}
  else if(next<target){from=std::max(value(now),target);target=next;started=now;}
  else if(next>target){from=target=next;}
  character=next_character;room=next_room;maximum=maxhp;return value(now);
 }
};
}
