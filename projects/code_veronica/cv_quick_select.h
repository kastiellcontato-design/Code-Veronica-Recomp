#pragma once
#include <cstdint>
#include "cv_items.h"
struct ppu_context;
extern "C" void cv_quick_pins_save(uint32_t packed);
namespace cv_quick {
struct Config {int on=0,key='V',button=4,edit_key='F',edit_button=3,confirm_key=13,confirm_button=0,keys[4]={'I','L','K','J'},items[4]={0,0,0,0};};
struct View {bool visible=false,editing=false;int selected=-1,room=-1,slots[4]={-1,-1,-1,-1},wanted[4]={},choices[12]={},choice_count=0,choice=0;uint64_t opened=0,closed=0;cv_items::Inventory inventory;Config config;};
Config config();void configure(const Config&);View view();void frame(ppu_context*);
bool equipable(int id);void resolve(View&);
void edit_choices(View&);bool assign_choice(View&);
// Called by the pad/keyboard adapter before mapping physical inputs.
int key_consumed(int vk);unsigned filter_buttons(unsigned);bool consumes_stick();
}
