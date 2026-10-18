#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../cv_srt.h"
#include "../cv_version.h"
#include "../cv_srt_text.h"
#include "../cv_srt_icons.h"
#include "../cv_srt_i18n.h"
#include <filesystem>
#include "../cv_launcher_controls.h"
#include "../launcher_i18n.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <cstdlib>
static std::vector<uint8_t> memory(16*1024*1024);
extern "C" {
uint8_t* vm_base=memory.data();
uint8_t vm_read8(uint64_t a) {assert(a<memory.size());return memory[a];}
uint16_t vm_read16(uint64_t a) {return uint16_t(vm_read8(a))<<8|vm_read8(a+1);}
uint32_t vm_read32(uint64_t a) {return uint32_t(vm_read16(a))<<16|vm_read16(a+2);}
}
static void put32(uint32_t a,uint32_t n) {for(int i=0;i<4;i++)memory[a+i]=uint8_t(n>>(24-i*8));}
static void put16(uint32_t a,uint16_t n) {memory[a]=uint8_t(n>>8);memory[a+1]=uint8_t(n);}
static void game() {
    std::fill(memory.begin(),memory.end(),0);
    put32(CV_SRT_RDX,0x3d650);put16(CV_SRT_ROOM,0x0501);put32(CV_SRT_TIME,65*60);
    put32(CV_SRT_HEALTH,160);put32(CV_SRT_SAVES,3);put16(CV_SRT_RETRY,2);put16(CV_SRT_FAS,1);
}
static void export_png(const char* path,const rsx_ui_quad* quads,uint32_t count,uint32_t w,uint32_t h,bool bright=false,bool crop=false) {
    using namespace Gdiplus;
    GdiplusStartupInput input;ULONG_PTR token;assert(GdiplusStartup(&token,&input,nullptr)==Ok);
    {
        int left=0,top=0;
        uint32_t ns=0,aw=0,ah=0;uint64_t gen=0;const uint32_t* rgba=nullptr;
        const rsx_ui_sprite* sprites=cv_srt_sprites(&ns,&rgba,&aw,&ah,&gen);
        if(crop&&count) {
            float x=float(w),y=float(h),right=0,bottom=0;
            for(uint32_t i=0;i<count;i++) {x=std::min(x,quads[i].x);y=std::min(y,quads[i].y);right=std::max(right,quads[i].x+quads[i].w);bottom=std::max(bottom,quads[i].y+quads[i].h);}
            for(uint32_t i=0;i<ns;i++) {x=std::min(x,sprites[i].x);y=std::min(y,sprites[i].y);right=std::max(right,sprites[i].x+sprites[i].w);bottom=std::max(bottom,sprites[i].y+sprites[i].h);}
            left=std::max(0,int(std::floor(x))-8);top=std::max(0,int(std::floor(y))-8);
            w=uint32_t(std::ceil(right)+8-left);h=uint32_t(std::ceil(bottom)+8-top);
        }
        Bitmap image(w,h,PixelFormat32bppARGB);Graphics g(&image);g.SetPixelOffsetMode(PixelOffsetModeHalf);g.Clear(Color(255,31,29,30));
        if(bright)for(uint32_t y=0;y<h;y+=64)for(uint32_t x=0;x<w;x+=64) {
            SolidBrush cell((x/64+y/64)%2?Color(255,224,214,195):Color(255,110,139,156));
            g.FillRectangle(&cell,int(x),int(y),64,64);
        }
        g.TranslateTransform(REAL(-left),REAL(-top));
        std::unique_ptr<Bitmap> atlas;
        if(ns) {
            atlas=std::make_unique<Bitmap>(int(aw),int(ah),PixelFormat32bppARGB);
            Rect r(0,0,int(aw),int(ah));BitmapData d{};assert(atlas->LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&d)==Ok);
            for(uint32_t y=0;y<ah;y++)for(uint32_t x=0;x<aw;x++) {
                const uint32_t c=rgba[size_t(y)*aw+x];
                reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(d.Scan0)+y*d.Stride)[x]=(c&0xff00ff00u)|(c>>16&255)|(c&255)<<16;
            }
            atlas->UnlockBits(&d);
        }
        uint32_t next=0;
        auto images=[&](uint32_t upto) {
            for(;next<ns&&sprites[next].after<=upto;next++) {
                const auto& s=sprites[next];const GraphicsState saved=g.Save();g.SetInterpolationMode(InterpolationModeNearestNeighbor);
                g.DrawImage(atlas.get(),RectF(s.x,s.y,s.w,s.h),s.u0*aw,s.v0*ah,(s.u1-s.u0)*aw,(s.v1-s.v0)*ah,UnitPixel);
                g.Restore(saved);
            }
        };
        for(uint32_t i=0;i<count;i++) {images(i);const auto& q=quads[i];SolidBrush b(Color(q.argb));g.FillRectangle(&b,q.x,q.y,q.w,q.h);}
        images(count);
        UINT bytes=0,n=0;GetImageEncodersSize(&n,&bytes);std::vector<uint8_t> data(bytes);
        auto* codecs=reinterpret_cast<ImageCodecInfo*>(data.data());GetImageEncoders(n,bytes,codecs);
        CLSID png{};for(UINT i=0;i<n;i++)if(!wcscmp(codecs[i].MimeType,L"image/png"))png=codecs[i].Clsid;
        const int size=MultiByteToWideChar(CP_UTF8,0,path,-1,nullptr,0);std::wstring name(size,0);MultiByteToWideChar(CP_UTF8,0,path,-1,name.data(),size);
        assert(image.Save(name.c_str(),&png)==Ok);
    }
    GdiplusShutdown(token);
}
static void test_item_wrapping() {
    using namespace Gdiplus;
    GdiplusStartupInput input;ULONG_PTR token;assert(GdiplusStartup(&token,&input,nullptr)==Ok);
    {
        Bitmap bitmap(1,1,PixelFormat32bppARGB);Graphics graphics(&bitmap);
        Font font(L"Segoe UI",13,FontStyleRegular,UnitPixel);
        auto measure=[&](const std::wstring& text) {RectF bounds;graphics.MeasureString(text.c_str(),-1,&font,PointF(0,0),&bounds);return bounds.Width+2;};
        auto letters=[](std::wstring value) {value.erase(std::remove_if(value.begin(),value.end(),[](wchar_t c){return std::iswspace(c)!=0;}),value.end());return value;};
        for(float width:{84.f,118.f,194.f,208.f})for(const wchar_t* value:{
            L"Munição de pistola",L"Munição de metralhadora",L"Spray de primeiros socorros",
            L"Objeto da formiga rainha",L"Granadas incendiárias",L"Flechas explosivas",
            L"Handgun Bullets",L"Machine Gun Bullets",L"First Aid Spray",L"Submachine Guns",
            L"ABCDEFGHIJKLMNOPQRSTUVWXYZ",L"ação coração atenção"}) {
            const auto lines=cv_srt_text::wrap(value,width,measure);std::wstring combined;
            for(const auto& line:lines) {assert(measure(line)<=width+.01f);assert(line.find(L"...")==std::wstring::npos);combined+=line;}
            assert(letters(combined)==letters(value));
        }
        assert(cv_srt_text::wrap(L"Munição de pistola",84,measure).size()>=2);
        assert(cv_srt_text::wrap(L"Handgun Bullets",84,measure).size()>=2);
        assert(cv_srt_text::wrap(L"Isqueiro",84,measure).size()==1);
    }
    GdiplusShutdown(token);
}
int main(int argc,char** argv) {
    test_item_wrapping();
    namespace loc=cv_srt_i18n;
    assert(loc::selection(0,LANG_JA)==loc::JA&&loc::selection(1,LANG_JA)==loc::EN&&loc::selection(2,LANG_EN)==loc::PT);
    for(int choice=3;choice<=LANG_COUNT;choice++)assert(int(loc::selection(choice,LANG_PT))==choice-1);
    assert(loc::quantity({9,10,0,1,0})==L"× 10");
    assert(loc::quantity({9,0,0,1,0})==L"× 0"&&loc::quantity({9,10,0,1,1})==L"∞");
    assert(loc::quantity({2,48,1,1,0})==L"48%"&&loc::quantity({0x22,25,1,1,0})==L"25%"&&loc::quantity({0x8e,100,1,1,0})==L"100%");
    assert(loc::quantity({8,0,0,0,0}).empty()&&loc::quantity({0,0,0,0,0}).empty());
    assert(loc::rtl_text(L"ذخيرة")&&!loc::rtl_text(L"× 10")&&!loc::rtl_text(L"Munição"));
    const wchar_t* handgun[]={L"Pistola",L"Handgun",L"Pistola",L"ハンドガン",L"Pistolet",L"Pistole",L"Pistola",L"Pistolet",L"Tabanca",L"مسدس",L"手枪",L"권총",L"Пистолет"};
    for(int lang=0;lang<LANG_COUNT;lang++) {
        const auto language=loc::Language(lang);assert(loc::language(k_lang_id[lang])==language);
        assert(loc::item(9,language)==handgun[lang]);assert(!loc::credit(language).empty());
        for(const auto& label:loc::labels(language))assert(!label.empty());
        for(int id=1;id<=0x99;id++) {
            const auto label=loc::item(id,language);assert(!label.empty()&&label.find(L'\ufffd')==std::wstring::npos);
            if(lang>=2&&id!=0x8d&&id!=0x92&&id!=0x93&&id!=0x94&&id!=0x97) {
                // Only the five unidentified IDs may fall back to "Unknown item (XX)".
                const auto unknown=loc::item(0x8d,language);assert(label.find(unknown.substr(0,unknown.find(L" (")))==std::wstring::npos);
            }
        }
        for(int enemy:{1,2,3,4,5,6,7,9,12,13,14,15,17,19,21,22,23,26,29,30})assert(!loc::enemy(enemy,language).empty());
    }
    for(const wchar_t* name:{L"SRT_CLAIRE",L"SRT_CHRIS",L"SRT_STEVE",L"SRT_WESKER",
        L"SRT_BOSSBAR_FILL",L"SRT_BOSSBAR_EMPTY",L"SRT_BOSSBAR_SMALL_FILL",L"SRT_BOSSBAR_SMALL_EMPTY",
        L"SRT_BOSS_TYRANT",L"SRT_BOSS_NOSFERATU",L"SRT_BOSS_STEVE",L"SRT_BOSS_ALBINOID",L"SRT_BOSS_GIANT_SPIDER",L"SRT_BOSS_GULP_WORM",
        L"SRT_BOSS_ALEXIA_1",L"SRT_BOSS_ALEXIA_2",L"SRT_BOSS_ALEXIA_3",L"SRT_BOSS_ANATOMIST",L"SRT_ENEMY_ZOMBIE",L"SRT_ENEMY_DOG",L"SRT_ENEMY_SPIDER",
        L"SRT_ENEMY_HUNTER",L"SRT_ENEMY_MOTH",L"SRT_ENEMY_BAT",L"SRT_ENEMY_BANDERSNATCH",L"SRT_ENEMY_ALBINOID",
        L"SRT_ENEMY_TENTACLE",L"SRT_ENEMY_ALEXIA_BABY"}) {
        const auto resource=FindResourceW(GetModuleHandleW(nullptr),name,MAKEINTRESOURCEW(10));
        assert(resource&&SizeofResource(GetModuleHandleW(nullptr),resource)<65536);
    }
    // Icon extraction is deterministic and never modifies the supplied archive.
    using namespace cv_srt_icons;
    Sprite sprite_rect{};assert(sprite(8,sprite_rect)&&sprite_rect.page==0&&sprite_rect.x==510&&sprite_rect.y==72);
    assert(sprite(0x37,sprite_rect)&&sprite_rect.x==510&&sprite_rect.y==576);
    assert(sprite(0x91,sprite_rect)&&sprite_rect.page==1&&sprite_rect.x==612&&sprite_rect.y==648);
    assert(sprite(1,sprite_rect)&&sprite_rect.w==204);
    assert(!sprite(0,sprite_rect)&&!sprite(0x26,sprite_rect)&&!sprite(0x6d,sprite_rect)&&!sprite(9999,sprite_rect));
    for(int id=1;id<0x9a;id++)if(sprite(id,sprite_rect))
        assert(sprite_rect.page>=0&&sprite_rect.page<2&&sprite_rect.x+sprite_rect.w<=768&&sprite_rect.y+sprite_rect.h<=720);
    std::vector<uint32_t> source(768*768,0xff080052);
    source[10*768+10]=0xff0080c0;source[12*768+12]=0xff2020a0;
    const auto original=source;const auto cut=extract(source,{0,0,0,102,72});
    assert(source==original&&cut.width==3&&cut.height==3&&cut.argb[0]==0xff0080c0&&cut.argb[4]==0&&cut.argb[8]==0xff2020a0);
    assert(extract(source,{0,760,0,102,72}).argb.empty());
    assert(extract({},sprite_rect).argb.empty());
    assert(unpack_archive({}).empty()&&unpack_archive(std::vector<uint8_t>(30)).empty());
    std::vector<uint8_t> bad(88);std::memcpy(bad.data()+1,"CRA",3);bad[7]=2;
    assert(decode_atlas(bad,0).empty());bad[7]=1;std::memcpy(bad.data()+8,"tex0004_BM",10);
    bad[72]=0x24;bad[73]=0x1f;bad[74]=0x5d;bad[75]=0xeb;
    assert(decode_atlas(bad,0).empty()&&decode_atlas(bad,-1).empty()&&decode_atlas(bad,2).empty());
    Library fallback;assert(!fallback.load(L"")&&!fallback.get(8));
    // Optional read-only integration check against each region's own assets.
    // Build automation supplies this only on the developer's local machine.
    if(const char* base=std::getenv("CV_SRT_TEST_GAME_ROOT")) {
        const auto root=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base)))/CV_TITLE_ID;
        Library icons;assert(icons.load(root.wstring()));
        for(int id:{1,6,7,8,9,12,0x14,0x15,0x16,0x17,0x21,0x22,0x37,0x40,0x43,0x5e,0x91}) {
            const auto* icon=icons.get(id);assert(icon&&icon->width>0&&icon->height>0);
            size_t clear=0,visible=0;for(uint32_t c:icon->argb) {if(c>>24)visible++;else clear++;}
            assert(clear&&visible);assert(icons.get(id)==icon);
        }
        assert(!icons.get(0)&&!icons.get(0x6d));
        assert(icons.load(root.wstring()));assert(!icons.load(L"")&&!icons.get(8));
        const auto root_utf8=root.u8string();_putenv_s("PS3_VFS_ROOT",reinterpret_cast<const char*>(root_utf8.c_str()));
    }
    using namespace cv_launcher_controls;
    for(uint32_t chord:{srt_chord,menu_chord}) {
        assert(chord_active(chord,chord));assert(chord_active(chord|(1u<<PB_START),chord));
        for(int b=0;b<PB_COUNT;b++)if(chord&(1u<<b))assert(!chord_active(chord&~(1u<<b),chord));
    }
    assert(!chord_active((1u<<PB_L3)|(1u<<PB_R1),srt_chord));
    assert(!chord_active((1u<<PB_L2)|(1u<<PB_R3),menu_chord));
    for(const auto range:{std::pair<int,int>{50,200},{0,100}}) {
        for(int value=range.first;value<=range.second;value++)
            assert(slider_value(695+313*slider_fraction(value,range.first,range.second),695,313,range.first,range.second)==value);
        assert(slider_value(-1000,695,313,range.first,range.second)==range.first);
        assert(slider_value(10000,695,313,range.first,range.second)==range.second);
    }
    assert(std::wstring(srt_credit_text(LANG_PT))==L"Feito por Kastiel");
    assert(std::wstring(srt_credit_text(LANG_EN))==L"by Kastiel");
    for(int lang=0;lang<LANG_COUNT;lang++)assert(std::wstring(srt_credit_text(lang)).find(L"Kastiel")!=std::wstring::npos);
    assert(cv_srt_text::upper(L"Atenção") == L"ATENÇÃO");
    assert(cv_srt_text::upper(L"Inventário · munição · gás") == L"INVENTÁRIO · MUNIÇÃO · GÁS");
    assert(cv_srt_text::upper(L"ação, coração, çãõáéíóúâêôà") == L"AÇÃO, CORAÇÃO, ÇÃÕÁÉÍÓÚÂÊÔÀ");
    cv_srt_state state{};assert(!cv_srt_sample(&state));game();assert(cv_srt_sample(&state));
#ifdef CV_REGION_JP
    const int max=200,very_easy=400;
#else
    const int max=160,very_easy=320;
#endif
    assert(state.maximum==max&&state.seconds==65&&state.saves==3&&state.retry==2&&state.fas==1);
    const int hp[]={max,max*60/100,max*59/100,max*29/100,max*14/100};
    const int expected[]={SRT_FINE,SRT_FINE,SRT_CAUTION,SRT_WARNING,SRT_DANGER};
    for(int i=0;i<5;i++) {put32(CV_SRT_HEALTH,hp[i]);assert(cv_srt_sample(&state));assert(state.condition==expected[i]);}
    memory[CV_SRT_STATUS]=8;cv_srt_sample(&state);assert(state.condition==SRT_POISON);
    memory[CV_SRT_STATUS]=0x20;cv_srt_sample(&state);assert(state.condition==SRT_GAS);
    put32(CV_SRT_HEALTH,uint32_t(-1));cv_srt_sample(&state);assert(state.condition==SRT_DEAD&&state.hp==0);
    memory[CV_SRT_STATUS]=0;memory[CV_SRT_DIFFICULTY]=2;put32(CV_SRT_HEALTH,very_easy);cv_srt_sample(&state);assert(state.maximum==very_easy);
    memory[CV_SRT_CHARACTER]=2;put32(CV_SRT_RDX,0x3a4d0);
    const uint32_t inv=CV_SRT_INVENTORY+2*0x40;
    put32(inv,1);put32(inv+4,0x08030007);cv_srt_sample(&state);
    assert(state.character==2&&state.equipped==1&&state.inventory[0].id==3&&state.inventory[0].quantity==7&&state.inventory[0].wide&&state.inventory[0].infinite);
    put32(inv,1);put32(inv+4,0x0009000a);put32(inv+8,0x000c000f);cv_srt_sample(&state);
    assert(state.inventory[state.equipped-1].quantity==10&&state.inventory[1].quantity==15);
    assert(loc::quantity(state.inventory[state.equipped-1])==L"× 10");
    put32(inv+4,0x00090009);cv_srt_sample(&state);assert(state.inventory[state.equipped-1].quantity==9);
    put32(CV_SRT_ENEMY_COUNT,1);put16(CV_SRT_ENEMIES+4,19);memory[CV_SRT_ENEMIES+0xc]=1;memory[CV_SRT_ENEMIES+0xf]=1;
    put32(CV_SRT_ENEMIES+0x41c,600);cv_srt_sample(&state);assert(state.enemy_count==1&&state.enemies[0].maximum==700&&!state.enemies[0].estimated);
    put16(CV_SRT_ROOM,0x0302);put32(CV_SRT_ENEMIES+0x41c,400);cv_srt_sample(&state);assert(state.enemies[0].maximum==500);
    put16(CV_SRT_ENEMIES+4,1);put32(CV_SRT_ENEMIES+0x41c,100);cv_srt_sample(&state);
    put32(CV_SRT_ENEMIES+0x41c,70);cv_srt_sample(&state);assert(state.enemies[0].maximum==100&&state.enemies[0].hp==70&&state.enemies[0].estimated);
    const auto before=memory;cv_srt_sample(&state);assert(before==memory);
    put32(CV_SRT_RDX,0);assert(!cv_srt_sample(&state)&&!state.valid&&state.enemy_count==0);
    game();put32(CV_SRT_HEALTH,max);cv_srt_sample(&state);
    _putenv_s("SRT","1");_putenv_s("SRT_STATS","1");_putenv_s("SRT_INVENTORY","1");
    _putenv_s("SRT_LANGUAGE","pt-BR");
    _putenv_s("SRT_TEXT","Vida|Tempo de jogo|Saves|Tentativas|Inimigos|Inventário|Equipado|Bem|Atenção|Perigo|Envenenado|Gás|Morto|Aguardando uma partida|Vazio");
    state.equipped=1;state.inventory[0]={8,0,0,0,0};state.inventory[1]={0x37,0,0,0,0};state.inventory[2]={12,47,0,1,0};state.inventory[3]={21,0,0,0,0};
    state.enemy_count=2;state.enemies[0]={1,73,100,1};state.enemies[1]={4,38,80,1};
    uint32_t count=0;unsigned peak=0;
    for(const char* corner:{"tl","tr","bl","br"})for(const char* scale:{"50","100","200"}) {
        _putenv_s("SRT_CORNER",corner);_putenv_s("SRT_SCALE",scale);cv_srt_configure_env();
        for(const auto& size:std::vector<std::pair<uint32_t,uint32_t>>{{1280,720},{1920,1080},{3840,2160}}) {
            const auto* q=cv_srt_design(&state,size.first,size.second,&count);assert(q&&count&&count<131072);peak=std::max(peak,count);
            for(uint32_t i=0;i<count;i++)assert(q[i].x>=15.9f&&q[i].y>=15.9f&&q[i].x+q[i].w<=size.first-15.9f&&q[i].y+q[i].h<=size.second-15.9f);
            // The HP bar (and ECG) must be the outside column, on every corner.
            const rsx_ui_quad* bar=nullptr;
            for(uint32_t i=0;i<count;i++)if((q[i].argb>>24)==255&&std::abs(int((q[i].argb>>16)&255)-121)<=12&&std::abs(int((q[i].argb>>8)&255)-187)<=12&&std::abs(int(q[i].argb&255)-145)<=12&&(!bar||q[i].w>bar->w))bar=&q[i];
            assert(bar);const bool right=corner[1]=='r';const float hud_scale=float(size.second)/900.f*std::atoi(scale)/100.f;
            const float group_center=right?size.first-16-310*hud_scale:16+310*hud_scale;
            assert(right?bar->x+bar->w*.5f>group_center:bar->x+bar->w*.5f<group_center);
            const unsigned animated=6*unsigned(std::floor(166*hud_scale));
            assert(animated<count&&std::abs(q[count-animated].x-bar->x)<=1.5f);
            uint32_t cached=0;assert(cv_srt_design(&state,size.first,size.second,&cached)==q&&cached==count);
            if(argc>1&&size.first==1280&&!strcmp(scale,"100")) {
                char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-corner-%s.png",argv[1],CV_TITLE_ID,corner);export_png(file,q,count,size.first,size.second);
            }
        }
    }
    // Long names, extreme size, and fully opaque/transparent panels must not
    // exceed the renderer budget even after adding the antialias shadows.
    for(const char* opacity:{"0","100"}) {
        _putenv_s("SRT_OPACITY",opacity);_putenv_s("SRT_ENEMIES","5");_putenv_s("SRT_SCALE","200");cv_srt_configure_env();
        state.enemy_count=5;state.condition=SRT_POISON;
        for(auto& e:state.enemies)e={26,9999,10000,1};
        const auto* q=cv_srt_design(&state,3840,2160,&count);assert(q&&count<131072);peak=std::max(peak,count);
    }
    // Eleven detailed icons at the largest supported scale still fit the batch.
    cv_srt_item saved_inventory[CV_SRT_INVENTORY_SIZE];std::memcpy(saved_inventory,state.inventory,sizeof saved_inventory);
    const int full_inventory[]={0x37,0x38,0x3d,0x39,0x4c,0x6c,0x42,0x55,0x14,0x75,0x91};
    for(int i=0;i<CV_SRT_INVENTORY_SIZE;i++)state.inventory[i]={full_inventory[i],0,0,0,0};
    const auto* detailed=cv_srt_design(&state,3840,2160,&count);assert(detailed&&count<131072);peak=std::max(peak,count);
    if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-full-inventory-4k.png",argv[1],CV_TITLE_ID);export_png(file,detailed,count,3840,2160);}
    std::memcpy(state.inventory,saved_inventory,sizeof saved_inventory);
    state.condition=SRT_FINE;_putenv_s("SRT_OPACITY","70");
    state.enemy_count=64;for(auto& e:state.enemies)e={26,9999,10000,1};
    _putenv_s("SRT_ENEMIES","99");cv_srt_configure_env();
    const auto* crowded=cv_srt_design(&state,1280,720,&count);assert(crowded&&count<131072);peak=std::max(peak,count);
    state.enemy_count=2;state.enemies[0]={1,73,100,1};state.enemies[1]={4,38,80,1};_putenv_s("SRT_ENEMIES","3");
    _putenv_s("SRT_SCALE","100");_putenv_s("SRT_CORNER","tl");cv_srt_configure_env();
    for(const char* opacity:{"0","25","70","100"}) {
        _putenv_s("SRT_OPACITY",opacity);cv_srt_configure_env();
        const auto* q=cv_srt_design(&state,1280,720,&count);int background_alpha=0;
        for(uint32_t i=0;i<count;i++)if(q[i].x<=22.5f&&q[i].y<=22.5f&&q[i].x+q[i].w>22.5f&&q[i].y+q[i].h>22.5f)background_alpha=q[i].argb>>24;
        assert(std::abs(background_alpha-int(std::lround(255*std::atoi(opacity)/100.f)))<=1);
        bool thick_enemy_bar=false;float name_shadow=0;
        for(uint32_t i=0;i<count;i++) {
            if(q[i].argb==0xffa3332c&&q[i].w>80&&q[i].h>=7)thick_enemy_bar=true;
            if((q[i].argb&0xffffff)==0&&(q[i].argb>>24)>30&&q[i].x>=30&&q[i].x<240&&q[i].y>=25&&q[i].y+q[i].h<44)
                name_shadow+=q[i].w*q[i].h;
        }
        assert(thick_enemy_bar);
        if(std::atoi(opacity)==0) {
            assert(name_shadow>20); // Letter shadows remain with the panel switched off.
            if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-shadow-bright.png",argv[1],CV_TITLE_ID);export_png(file,q,count,1280,720,true);}
        }
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-opacity-%s.png",argv[1],CV_TITLE_ID,opacity);export_png(file,q,count,1280,720);}
    }
    _putenv_s("SRT_OPACITY","70");cv_srt_configure_env();
    cv_srt_state waiting{};
    const auto* notice=cv_srt_design(&waiting,1280,720,&count);
    for(uint32_t i=0;i<count;i++)assert(notice[i].x+notice[i].w<=225&&notice[i].y+notice[i].h<=53);
    if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-waiting.png",argv[1],CV_TITLE_ID);export_png(file,notice,count,1280,720);}
    for(int c=0;c<7;c++) {
        state.condition=c;state.hp=c==SRT_FINE?max:c==SRT_CAUTION?max/2:c==SRT_WARNING?max/4:c==SRT_DEAD?0:max/10;
        const auto* q=cv_srt_design(&state,1280,720,&count);
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-state-%d.png",argv[1],CV_TITLE_ID,c);export_png(file,q,count,1280,720);}
    }
    for(int lang=0;lang<LANG_COUNT;lang++) {
        const wchar_t* credit=srt_credit_text(lang);
        int length=WideCharToMultiByte(CP_UTF8,0,credit,-1,nullptr,0,nullptr,nullptr);
        std::string encoded(size_t(length),0);WideCharToMultiByte(CP_UTF8,0,credit,-1,encoded.data(),length,nullptr,nullptr);encoded.pop_back();
        _putenv_s("SRT_CREDIT",encoded.c_str());cv_srt_configure_env();
        const auto* localized=cv_srt_design(&state,1280,720,&count);assert(localized&&count&&count<131072);
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-credit-%s.png",argv[1],CV_TITLE_ID,k_lang_id[lang]);export_png(file,localized,count,1280,720);}
    }
    _putenv_s("SRT_CREDIT","");cv_srt_configure_env();
    const auto localized_saved=state;
    const std::string saved_words=std::getenv("SRT_TEXT");
    state.hp=max;state.condition=SRT_FINE;state.equipped=2;
    state.inventory[0]={0x37,0,0,0,0};state.inventory[1]={9,10,0,1,0};state.inventory[2]={12,15,0,1,0};state.inventory[3]={0x91,0,0,0,0};
    _putenv_s("SRT_TEXT","");
    for(int lang=0;lang<LANG_COUNT;lang++) {
        _putenv_s("SRT_LANGUAGE",k_lang_id[lang]);cv_srt_configure_env();
        const auto* localized=cv_srt_design(&state,1600,900,&count);assert(localized&&count<131072);peak=std::max(peak,count);
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-locale-%s.png",argv[1],CV_TITLE_ID,k_lang_id[lang]);export_png(file,localized,count,1600,900,false,true);}
    }
    _putenv_s("SRT_LANGUAGE","pt-BR");
    for(int character=0;character<4;character++)for(int ammo:{10,0}) {
        state.character=character;state.inventory[1].quantity=ammo;cv_srt_configure_env();
        const auto* preview=cv_srt_design(&state,1600,900,&count);assert(preview&&count<131072);
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-character-%d-ammo-%d.png",argv[1],CV_TITLE_ID,character,ammo);export_png(file,preview,count,1600,900,false,true);}
    }
    // Percent magazine and infinite ammunition have the same presentation in
    // the inventory and equipped block; no totals or capacities are invented.
    state.character=0;state.inventory[1]={2,48,1,1,0};cv_srt_configure_env();
    const auto* percent=cv_srt_design(&state,1600,900,&count);assert(percent&&count<131072);
    if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-equipped-percent.png",argv[1],CV_TITLE_ID);export_png(file,percent,count,1600,900,false,true);}
    state.inventory[1].infinite=1;const auto* infinite=cv_srt_design(&state,1600,900,&count);assert(infinite&&count<131072);
    // Bosses go to the top of the list in their framed block, inside the enemy limit.
    {
        const auto saved_state=state;
        const std::string saved_limit=std::getenv("SRT_ENEMIES")?std::getenv("SRT_ENEMIES"):"3";
        state.enemy_count=4;state.enemies[0]={1,72,72,0};state.enemies[1]={4,38,80,1};_putenv_s("SRT_ENEMIES","5");state.enemies[2]={19,504,700,0};state.enemies[3]={13,266,700,0};
        for(const char* lang:{"pt-BR","en","ja","ar"}) {
            _putenv_s("SRT_LANGUAGE",lang);cv_srt_configure_env();
            const auto* bosses=cv_srt_design(&state,1600,900,&count);assert(bosses);assert(count<131072);peak=std::max(peak,count);
            if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-bosses-%s.png",argv[1],CV_TITLE_ID,lang);export_png(file,bosses,count,1600,900,false,true);}
        }
        for(int b:{2,12,14,15,17,22,23,26}) {state.enemy_count=1;state.enemies[0]={b,10,100,0};assert(cv_srt_design(&state,3840,2160,&count)&&count<131072);peak=std::max(peak,count);}
        // The drawn bar: under the panel's quads and the life's glow, the frame last.
        {
            state.enemy_count=2;state.enemies[0]={19,504,700,0};state.enemies[1]={1,72,72,0};
            _putenv_s("SRT_LANGUAGE","pt-BR");_putenv_s("SRT_BOSS_BAR","panel");cv_srt_configure_env();
            const auto* q=cv_srt_design(&state,1600,900,&count);assert(q&&count<131072);
            uint32_t ns=0,aw=0,ah=0;uint64_t gen=0;const uint32_t* rgba=nullptr;
            const rsx_ui_sprite* sp=cv_srt_sprites(&ns,&rgba,&aw,&ah,&gen);
            assert(sp&&ns==3&&rgba&&aw&&ah);
            assert(sp[0].after<=sp[2].after&&sp[2].after==count);
            assert(std::fabs(sp[1].w-sp[0].w*504/700.f)<=1.f);   // the life: 72% of the channel
            for(const char* where:{"top","bottom"})for(const auto& size:std::vector<std::pair<uint32_t,uint32_t>>{{1280,720},{1600,900},{3840,2160}}) {
                _putenv_s("SRT_BOSS_BAR",where);cv_srt_configure_env();
                const auto* c=cv_srt_design(&state,size.first,size.second,&count);assert(c&&count<131072);peak=std::max(peak,count);
                sp=cv_srt_sprites(&ns,&rgba,&aw,&ah,&gen);assert(sp&&ns==3);
                // centred, inside the screen, at the chosen edge
                // (at the top, beside the panel and its inventory on the same edge)
                const float mid=sp[2].x+sp[2].w*.5f;
                if(!strcmp(where,"bottom"))assert(std::fabs(mid-size.first*.5f)<=1.f);
                else assert(sp[2].x>=16+620*size.second/900.f-1);
                assert(sp[2].x>=0&&sp[2].x+sp[2].w<=size.first&&sp[2].y>=0&&sp[2].y+sp[2].h<=size.second);
                if(!strcmp(where,"top"))assert(sp[2].y<size.second*.25f);else assert(sp[2].y>size.second*.6f);
                for(uint32_t i=0;i<count;i++)assert(c[i].x>=0&&c[i].y>=0&&c[i].x+c[i].w<=size.first&&c[i].y+c[i].h<=size.second);
                if(argc>1&&size.first==1600) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-boss-bar-%s.png",argv[1],CV_TITLE_ID,where);export_png(file,c,count,1600,900,true,false);}
            }
            // Alexia: the phase under the bar, in Japanese too
            state.enemies[0]={13,266,700,0};
            for(const char* lang:{"pt-BR","ja"}) {
                _putenv_s("SRT_LANGUAGE",lang);cv_srt_configure_env();
                const auto* c=cv_srt_design(&state,1600,900,&count);assert(c&&count<131072);
                if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-boss-bar-alexia-%s.png",argv[1],CV_TITLE_ID,lang);export_png(file,c,count,1600,900,true,false);}
            }
            // The Anatomist zombie, a sub-boss: the bar in the middle as the bosses.
            state.enemies[0]={26,150,200,0};_putenv_s("SRT_BOSS_BAR","top");cv_srt_configure_env();
            {
                const auto* c=cv_srt_design(&state,1600,900,&count);assert(c&&count<131072);
                sp=cv_srt_sprites(&ns,&rgba,&aw,&ah,&gen);assert(sp&&ns==3&&std::fabs(sp[1].w-sp[0].w*150/200.f)<=1.f);
                if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-boss-bar-anatomist.png",argv[1],CV_TITLE_ID);export_png(file,c,count,1600,900,true,false);}
            }
            _putenv_s("SRT_BOSS_BAR","panel");_putenv_s("SRT_LANGUAGE","pt-BR");
        }
        state=saved_state;_putenv_s("SRT_ENEMIES",saved_limit.c_str());_putenv_s("SRT_LANGUAGE","pt-BR");cv_srt_configure_env();
    }
    // The fixed slot (entry 0, the lighter) sits above the equipped weapon; the
    // inventory can also go under the enemies, in the same column.
    {
        const auto saved_state=state;
        const std::string saved_corner=std::getenv("SRT_CORNER")?std::getenv("SRT_CORNER"):"tl";
        state.inventory[0]={0x37,0,0,0,0};state.inventory[1]={9,10,0,1,0};state.inventory[2]={12,15,0,1,0};state.inventory[3]={0x91,0,0,0,0};state.equipped=2;
        state.enemy_count=2;state.enemies[0]={19,504,700,0};state.enemies[1]={1,72,72,0};
        for(const char* where:{"side","below"})for(const char* corner:{"tl","br"}) {
            _putenv_s("SRT_INVENTORY_POS",where);_putenv_s("SRT_CORNER",corner);cv_srt_configure_env();
            for(const auto& size:std::vector<std::pair<uint32_t,uint32_t>>{{1280,720},{1600,900},{3840,2160}}) {
                const auto* q=cv_srt_design(&state,size.first,size.second,&count);assert(q&&count<131072);peak=std::max(peak,count);
                for(uint32_t i=0;i<count;i++)assert(q[i].x>=15.9f&&q[i].y>=15.9f&&q[i].x+q[i].w<=size.first-15.9f&&q[i].y+q[i].h<=size.second-15.9f);
                if(argc>1&&size.first==1600) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-inventory-%s-%s.png",argv[1],CV_TITLE_ID,where,corner);export_png(file,q,count,1600,900,false,true);}
            }
        }
        _putenv_s("SRT_INVENTORY_POS","side");_putenv_s("SRT_CORNER",saved_corner.c_str());state=saved_state;cv_srt_configure_env();
    }
    state=localized_saved;_putenv_s("SRT_LANGUAGE","pt-BR");_putenv_s("SRT_TEXT",saved_words.c_str());cv_srt_configure_env();
    state.condition=SRT_FINE;state.hp=max;
    // Preview from the production renderer, using synthetic inventory only.
    // Three two-slot weapons must each occupy one full-width row, followed by
    // narrow ammo/knife slots. No writes to guest inventory or real settings.
    const auto preview_saved=state;
    // Entry 0 (the fixed slot) stays empty here, so the grid starts at the top.
    state.equipped=2;std::fill(std::begin(state.inventory),std::end(state.inventory),cv_srt_item{});
    state.inventory[1]={0x21,100,1,1,0};state.inventory[2]={0x22,100,1,1,0};state.inventory[3]={1,4,1,1,0};
    state.inventory[4]={12,47,0,1,0};state.inventory[5]={8,0,0,0,0};state.inventory[6]={0x37,0,0,0,0};state.inventory[7]={0x91,0,0,0,0};
    _putenv_s("SRT_SCALE","100");_putenv_s("SRT_CORNER","tl");
    const std::string pt_labels=std::getenv("SRT_TEXT");
    for(const char* language:{"pt-BR","en"}) {
        _putenv_s("SRT_LANGUAGE",language);_putenv_s("SRT_TEXT",!strcmp(language,"en")?"":pt_labels.c_str());cv_srt_configure_env();
        const auto* preview=cv_srt_design(&state,1600,900,&count);assert(preview&&count<131072);peak=std::max(peak,count);
        auto pixel=[&](float x,float y) {
            uint32_t color=0;for(uint32_t i=0;i<count;i++)if(preview[i].x<=x&&preview[i].y<=y&&preview[i].x+preview[i].w>x&&preview[i].y+preview[i].h>y)color=preview[i].argb;
            return color;
        };
        // The middle gap is filled by each two-slot weapon's cell. It stays
        // open between two ordinary slots. Sample clear space below the text.
        for(float y:{145.5f,185.5f,225.5f})assert(pixel(351.5f,y)==pixel(484.5f,y));
        assert(pixel(351.5f,282.5f)!=pixel(484.5f,282.5f));
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-two-slots-%s.png",argv[1],CV_TITLE_ID,language);export_png(file,preview,count,1600,900,false,true);}
        state.equipped=5;const auto* ammo=cv_srt_design(&state,1600,900,&count);assert(ammo&&count<131072);
        if(argc>1) {char file[1024];std::snprintf(file,sizeof file,"%s/srt-%s-wrapped-ammo-%s.png",argv[1],CV_TITLE_ID,language);export_png(file,ammo,count,1600,900,false,true);}
        state.equipped=2;
    }
    state=preview_saved;_putenv_s("SRT_LANGUAGE","pt-BR");_putenv_s("SRT_TEXT",pt_labels.c_str());cv_srt_configure_env();
    const auto* animated=cv_srt_design(&state,1280,720,&count);
    const auto initial=std::vector<rsx_ui_quad>(animated,animated+count);const uint32_t original_count=count;
    Sleep(65);animated=cv_srt_design(&state,1280,720,&count);
    const uint32_t pulse_quads=6*uint32_t(166*720.f/900.f);
    assert(count==original_count&&count>pulse_quads);
    assert(!std::memcmp(initial.data(),animated,(count-pulse_quads)*sizeof(rsx_ui_quad)));
    assert(std::memcmp(initial.data()+count-pulse_quads,animated+count-pulse_quads,pulse_quads*sizeof(rsx_ui_quad)));
    state.condition=SRT_DEAD;state.hp=0;animated=cv_srt_design(&state,1280,720,&count);
    const auto flat=std::vector<rsx_ui_quad>(animated,animated+count);
    Sleep(65);animated=cv_srt_design(&state,1280,720,&count);
    assert(!std::memcmp(flat.data(),animated,count*sizeof(rsx_ui_quad)));
    LARGE_INTEGER frequency,start,stop;
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    for(int i=0;i<20;i++) {state.seconds++;cv_srt_design(&state,1280,720,&count);}
    QueryPerformanceCounter(&stop);std::printf("[SRT-BENCH %s] changing timer/layout: %.3f ms/call\n",CV_TITLE_ID,1000.0*(stop.QuadPart-start.QuadPart)/frequency.QuadPart/20);QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    for(int i=0;i<10000;i++)cv_srt_design(&state,1280,720,&count);
    QueryPerformanceCounter(&stop);std::printf("[SRT-BENCH %s] cached layout + ECG: %.4f ms/call\n",CV_TITLE_ID,1000.0*(stop.QuadPart-start.QuadPart)/frequency.QuadPart/10000);
    _putenv_s("SRT","0");cv_srt_configure_env();assert(cv_srt_design(&state,1280,720,&count)==nullptr&&count==0);
    std::printf("[SRT-TEST %s] PASS: big-endian HP/status/characters/inventory/enemies, no guest writes, 13 full locales/RTL/CJK/four embedded portraits/equipped ammo + percent/empty/infinite, full wrapped item names + separate quantities/two-slot preview, transparent original item icons/safe atlas fallback/mirrored main and ECG, triple-button shortcuts/1-percent sliders/13 credits, Unicode PT-BR/opacity/foreground shadows/thick enemy bars/compact notice, cached design/animated ECG/dead flatline, 4 corners x 3 scales x 3 resolutions; peak=%u quads\n",CV_TITLE_ID,peak);
}
