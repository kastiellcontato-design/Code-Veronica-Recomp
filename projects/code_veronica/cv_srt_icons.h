#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Original game images are read from the selected installation, never bundled.
// This module owns CPU pixels only; the overlay owns its GDI drawing cache.
namespace cv_srt_icons {
struct Sprite { int page,x,y,w,h; };
struct Icon { int width=0,height=0;std::vector<uint32_t> argb; };
bool sprite(int id,Sprite& out);
std::vector<uint8_t> unpack_archive(const std::vector<uint8_t>& file);
std::vector<uint32_t> decode_atlas(const std::vector<uint8_t>& archive,int page);
Icon extract(const std::vector<uint32_t>& atlas,const Sprite& rect);
class Library {
    bool attempted_=false;
    std::wstring root_;
    std::array<std::vector<uint32_t>,2> atlas_;
    std::array<Icon,0x9a> icons_;
public:
    bool load(const std::wstring& game_root);
    const Icon* get(int id);
};
}
