#pragma once
#include "cv_appearance_assets.h"
struct ppu_context;
namespace cv_appearance {
// Called only on the game's PPU thread. Selection previews are independent of
// this explicit application request. The original actor's identity never changes.
bool request(ppu_context*,unsigned,const PlayerAsset*);
void frame(ppu_context*,bool in_menu=false);
unsigned applied();
bool busy();
const char* status();
}
