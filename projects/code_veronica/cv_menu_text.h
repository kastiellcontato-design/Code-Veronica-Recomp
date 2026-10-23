#pragma once
#include <gdiplus.h>
#include "cv_enhanced_settings.h"
namespace cv_menu_text {
struct Cell {float x,y,w;uint32_t corners;};
struct Raster {float width=0;std::vector<Cell> cells;};
inline const Raster& raster(const std::string& value){
    static std::map<std::pair<int,std::string>,Raster> cache;
    int lang=cv_enhanced_language();auto key=std::make_pair(lang,value);auto found=cache.find(key);if(found!=cache.end())return found->second;
    static struct Init{ULONG_PTR token{};Init(){Gdiplus::GdiplusStartupInput i;Gdiplus::GdiplusStartup(&token,&i,nullptr);}~Init(){Gdiplus::GdiplusShutdown(token);}} init;
    int n=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);std::wstring wide(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),wide.data(),n);
    Gdiplus::Bitmap bitmap(1800,52,PixelFormat32bppARGB);Gdiplus::Graphics gr(&bitmap);gr.Clear(Gdiplus::Color(0,0,0,0));gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const wchar_t* face=lang==3?L"Yu Mincho":lang==10?L"Microsoft YaHei":lang==11?L"Malgun Gothic":lang==9?L"Segoe UI":L"Times New Roman";
    Gdiplus::FontFamily family(face);
    // Optional language packs may omit a preferred face. A valid base font
    // lets GDI+ use Windows glyph fallback instead of hiding the whole menu.
    const auto* available=family.GetLastStatus()==Gdiplus::Ok?&family:Gdiplus::FontFamily::GenericSansSerif();
    Gdiplus::Font font(available,27,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());format.SetFormatFlags(format.GetFormatFlags()|Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::RectF measured;gr.MeasureString(wide.data(),n,&font,Gdiplus::PointF(2,0),&format,&measured);
    Gdiplus::SolidBrush white(Gdiplus::Color::White);gr.DrawString(wide.data(),n,&font,Gdiplus::PointF(2,0),&format,&white);
    Raster out;out.width=measured.Width+4;Gdiplus::BitmapData pixels{};Gdiplus::Rect bounds(0,0,1800,52);bitmap.LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&pixels);
    const int width=std::min(1799,int(std::ceil(out.width))+2);
    auto cov=[&](int x,int y)->uint32_t{if(x<0||y<0||x>=width||y>=52)return 0;return static_cast<unsigned char*>(pixels.Scan0)[y*pixels.Stride+x*4+3];};
    auto corners=[&](int x,int y){return cov(x,y)|cov(x,y+1)<<8|cov(x+1,y)<<16|cov(x+1,y+1)<<24;};
    for(int y=-1;y<52;y++)for(int x=-1;x<width;){uint32_t c=corners(x,y);if(!c){x++;continue;}int end=x+1;if((c&255)*0x01010101u==c)while(end<width&&corners(end,y)==c)end++;out.cells.push_back({x+.5f,y+.5f,float(end-x),c});x=end;}
    bitmap.UnlockBits(&pixels);return cache.emplace(std::move(key),std::move(out)).first->second;
}
struct TextBatch {
 std::vector<rsx_ui_quad> quads;
 void text(const std::string& value,float x,float y,uint32_t color,float scale=1,bool centered=false,float max_width=576){
  const auto& r=raster(value);if(r.width*scale>max_width)scale=max_width/r.width;if(centered)x-=r.width*scale/2;
  for(int pass=0;pass<2;pass++)for(const auto& c:r.cells)quads.push_back({x+(c.x+(pass?0:2))*scale,y+(c.y+(pass?0:2))*scale,c.w*scale,scale,pass?color:(color&0xFF000000u),c.corners});
 }
 void submit(const ppu_context*){rsx_d3d12_set_ui_quads(quads.data(),uint32_t(quads.size()));}
};
}
