// Native inventory thresholds are absolute HP, not percentages of regional max.
#include "../cv_combat_hud_model.h"
#include <cassert>
int main(){using namespace cv_combat_hud;
 for(int maximum:{160,200,320,400}){(void)maximum;
  assert(health_condition(120,SRT_FINE)==SRT_FINE);assert(health_condition(119,SRT_FINE)==SRT_CAUTION);
  assert(health_condition(60,SRT_FINE)==SRT_CAUTION);assert(health_condition(59,SRT_FINE)==SRT_WARNING);
  assert(health_condition(30,SRT_FINE)==SRT_WARNING);assert(health_condition(29,SRT_FINE)==SRT_DANGER);
  assert(health_condition(119,SRT_POISON)==SRT_POISON);assert(health_condition(119,SRT_GAS)==SRT_GAS);
 }
 assert(health_condition(0,SRT_FINE)==SRT_DEAD);
}
