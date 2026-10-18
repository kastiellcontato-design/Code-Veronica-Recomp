/* Offline tests: actual icon recognizer/compositor; no running game or input. */
#include "../button_icons.cpp"
#include <cassert>
extern "C" {
uint8_t* vm_base=nullptr;
int pad_last_input(void){return PAD_INPUT_KEYBOARD;}
int pad_phys_poll(int,PadPhys*){return 0;}
void vfs_overlay_set(vfs_overlay_fn){}
void rsx_tex_mark_dirty(uint32_t){}
rsx_tex_source_fn g_rsx_tex_source_hook;
rsx_tex_ui_fn g_rsx_tex_ui_hook;
}
static void release(texpack_image* p){if(p){free(p->data);free(p);}}
static void png(const texpack_image* im,const std::wstring& path){
    using namespace Gdiplus;Bitmap b(im->w,im->h,PixelFormat32bppARGB);Rect r(0,0,im->w,im->h);BitmapData d;
    assert(b.LockBits(&r,ImageLockModeWrite,PixelFormat32bppARGB,&d)==Ok);
    for(uint32_t y=0;y<im->h;++y)for(uint32_t x=0;x<im->w;++x){const auto* p=im->data+((size_t)y*im->w+x)*4;
        auto* q=(uint8_t*)d.Scan0+(ptrdiff_t)y*d.Stride+x*4;q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];}
    b.UnlockBits(&d);UINT n=0,size=0;GetImageEncodersSize(&n,&size);std::vector<uint8_t> buf(size);
    auto* codecs=(ImageCodecInfo*)buf.data();GetImageEncoders(n,size,codecs);
    for(UINT i=0;i<n;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){assert(b.Save(path.c_str(),&codecs[i].Clsid,nullptr)==Ok);return;}
    assert(false);
}
int main(int argc,char** argv){
    setvbuf(stdout,nullptr,_IONBF,0);assert(argc==3);gdiplus_once();assert(hd_pad_sheet());
    const fs::path root=fs::u8path(argv[1]),out=fs::u8path(argv[2]);fs::create_directories(out);
    const auto guide=(root/"window/button_guide.arc").generic_string();load_pairs(guide,guide);assert(s_pairs.size()==2);
    int count=0;
    for(size_t i=0;i<2;++i)for(int set=0;set<3;++set){s_shown=set;auto& p=s_pairs[i];auto before=p.img[set];
        uint64_t last=0;
        for(float scale:{1.0f,1.5f,2.0f,3.0f,4.0f}){
            texpack_image* im=nullptr;uint64_t id=on_hd_image(p.img[set].data(),(uint32_t)p.img[set].size(),32,32,scale,0,&im);
            assert(id&&im&&im->levels==1);assert(im->w==32*(uint32_t)std::clamp((int)std::ceil(scale),2,4));
            assert(im->bytes==(size_t)im->w*im->h*4&&im->data[3]==0);
            size_t visible=0;for(size_t j=3;j<im->bytes;j+=4)visible+=im->data[j]>200;assert(visible>im->w*im->h/4);
            if(scale==3)png(im,(out/("prompt_"+std::to_string(i)+"_set_"+std::to_string(set)+".png")).wstring());
            release(im);im=nullptr;assert(on_hd_image(p.img[set].data(),(uint32_t)p.img[set].size(),32,32,scale,id,&im)==id&&!im);
            last=id;++count;
        }
        assert(p.img[set]==before);
        texpack_image* im=nullptr;
        assert(!on_hd_image(p.img[set].data(),(uint32_t)p.img[set].size(),64,64,1,last,&im)&&!im);
    }
    // Remapping invalidates keyboard art without altering either controller.
    const auto ps=s_pairs[0].img[SET_PS],xb=s_pairs[0].img[SET_XBOX],old=s_pairs[0].img[SET_KB];
    _putenv_s("PAD_KEYMAP","cross=90");make_keyboard(s_pairs[0]);assert(s_pairs[0].img[SET_KB]!=old);
    assert(ps==s_pairs[0].img[SET_PS]&&xb==s_pairs[0].img[SET_XBOX]);_putenv_s("PAD_KEYMAP","");
    // Reject unrelated contents even at a UI-sized recycled address.
    std::vector<uint8_t> alien(1024,0x5A);texpack_image* im=nullptr;
    assert(!on_hd_image(alien.data(),1024,32,32,2,0,&im)&&!im);
    _putenv_s("CV_HD_BUTTONS","0");assert(!on_hd_image(ps.data(),1024,32,32,2,0,&im)&&!im);_putenv_s("CV_HD_BUTTONS","");
    const auto screen=(root/"adv/screen_setting.arc").generic_string();load_pairs(screen,screen);assert(s_pairs.size()==3);
    auto& p=s_pairs[2];assert(p.box[2]>p.box[0]);
    for(int set=0;set<3;++set){s_shown=set;assert(on_hd_image(p.img[set].data(),(uint32_t)p.img[set].size(),648,128,3,0,&im));
        assert(im&&im->w==1944&&im->h==384);png(im,(out/("screen_set_"+std::to_string(set)+".png")).wstring());release(im);im=nullptr;}

    const auto map=(root/"mry/mp_dat0.arc").generic_string();const size_t map_i=s_pairs.size();load_map_pair(map,map);
    assert(s_pairs.size()==map_i+1);
    for(int set=0;set<3;++set){s_shown=set;auto& mp=s_pairs[map_i];
        assert(on_hd_image(mp.img[set].data(),(uint32_t)mp.img[set].size(),192,192,3,0,&im));
        assert(im&&im->w==576&&im->h==576);png(im,(out/("map_set_"+std::to_string(set)+".png")).wstring());release(im);im=nullptr;}
    const auto manual=(root/"on_manual/on_manual.arc").generic_string();const size_t man_i=s_pairs.size();load_pairs(manual,manual);
    build_keyboard_pages();assert(s_pairs.size()==man_i+3);

    // Every controller page is located independently. These are known native
    // landmarks, including JP's extra Circle action and different Fire position.
    const bool japanese=root.generic_string().find("/jpn")!=std::string::npos;
    if(japanese){
        const auto& jp=s_pairs[man_i];assert(jp.jp_manual&&jp.hd_keyboard.w>100);
        assert(jp.img[SET_KB]!=jp.img[SET_PS]&&target(jp,SET_KB)==SET_KB);
        for(int a:{PA_L1,PA_R1,PA_UP,PA_DOWN,PA_CROSS,PA_SQUARE,PA_CIRCLE,PA_TRIANGLE,PA_START,PA_SELECT}){
            bool recorded=false;for(const auto& k:jp.hd_keys)if(k.vk==key_for(a))recorded=true;assert(recorded);
        }
        for(int set:{SET_PS,SET_XBOX}){
            const auto controls=jp_manual_controls(jp,set);assert(controls.size()==8);
            const auto original=dxt1_decode(jp.img[set].data(),1280,720);auto clean=original;
            clear_manual_rects(clean,1280,720,controls);
            for(int y=0;y<720;++y)for(int x=0;x<1280;++x){size_t k=(size_t)y*1280+x;
                if(!memcmp(&original[k],&clean[k],sizeof(Rgba)))continue;
                bool inside=false;for(const auto& c:controls)if(x>=c.x&&x<c.x+c.w&&y>=c.y&&y<c.y+c.h)inside=true;
                assert(inside);
            }
        }
        // Controller illustrations stay exactly as in the original compositor;
        // only the surrounding instruction balloons receive new controls.
        for(int set:{SET_PS,SET_XBOX}){
            Pair reference=jp;reference.jp_manual=false;
            const auto actual=hd_image(jp,set,1,1280,720),native=hd_image(reference,set,1,1280,720);
            for(int y=260;y<560;++y)for(int x=450;x<832;++x){const size_t k=(size_t)y*1280+x;
                assert(!memcmp(&actual[k],&native[k],sizeof(Rgba)));}
        }
        // Remapping the JP page rebuilds both its keycaps and central diagram.
        Pair remapped=jp;_putenv_s("PAD_KEYMAP","square=120+121,up=73");make_keyboard(remapped);
        assert(remapped.img[SET_KB]!=jp.img[SET_KB]);bool f9=false;
        assert(remapped.hd_keyboard.lit.count(120)&&remapped.hd_keyboard.lit.count(121)&&remapped.hd_keyboard.lit.count(73));
        assert(!remapped.hd_keyboard.lit.count('W'));
        std::set<int> expected;
        for(int a:{PA_CROSS,PA_CIRCLE,PA_SQUARE,PA_TRIANGLE,PA_L1,PA_R1,PA_SELECT,PA_START,PA_UP,PA_DOWN,PA_LEFT,PA_RIGHT})
            for(int vk:keys_for(a))expected.insert(vk);
        assert(remapped.hd_keyboard.lit==expected);
        for(const auto& k:remapped.hd_keys)f9|=k.vk==120;assert(f9);
        assert(remapped.img[SET_PS]==jp.img[SET_PS]&&remapped.img[SET_XBOX]==jp.img[SET_XBOX]);_putenv_s("PAD_KEYMAP","");
    }else assert(!s_pairs[man_i].jp_manual&&jp_manual_controls(s_pairs[man_i],SET_PS).empty());
    auto verify=[&](int set,int action,float x,float y){
        const auto& page=s_pairs[man_i];const auto pixels=dxt1_decode(page.img[set].data(),1280,720);
        const auto found=manual_icons(page,set,pixels,1280,720);int count=0;
        for(const auto& m:found)if(m.action==action&&std::fabs(m.cx-x)<1&&std::fabs(m.cy-y)<1)++count;
        assert(count==1);
        for(size_t i=0;i<found.size();++i)for(size_t j=i+1;j<found.size();++j)
            assert(std::hypot(found[i].cx-found[j].cx,found[i].cy-found[j].cy)>32);
        auto cleaned=pixels;clear_manual_icons(cleaned,1280,720,found);
        // Every edit is confined to a detected original circle, never text.
        for(int yy=0;yy<720;++yy)for(int xx=0;xx<1280;++xx){
            const size_t k=(size_t)yy*1280+xx;if(!memcmp(&cleaned[k],&pixels[k],sizeof(Rgba)))continue;
            bool inside=false;for(const auto& m:found)if(std::hypot(xx-m.cx,yy-m.cy)<=m.side*.5f+1.01f)inside=true;
            assert(inside);
        }
    };
    if(japanese || root.filename()=="eng") {
        verify(SET_PS,PA_SQUARE,japanese?878.5f:838.5f,japanese?189.5f:207.5f);
        verify(SET_PS,PA_CROSS,japanese?926.5f:916.5f,japanese?466.5f:494.5f);
        if(japanese)verify(SET_PS,PA_CIRCLE,940.5f,189.5f);
        verify(SET_XBOX,PA_SQUARE,japanese?859.5f:816.5f,207.5f);
        verify(SET_XBOX,PA_CROSS,japanese?926.5f:916.5f,494.5f);
    }
    for(size_t page=man_i;page<s_pairs.size();++page)for(int set=0;set<3;++set){
        s_shown=set;auto& mp=s_pairs[page];
        if(set!=SET_KB){
            const auto pixels=dxt1_decode(mp.img[set].data(),1280,720);
            const auto found=manual_icons(mp,set,pixels,1280,720);
            std::printf("[MANUAL] %s page=%d set=%d count=%zu\n",root.filename().string().c_str(),mp.manual_page,set,found.size());
            for(const auto& m:found)std::printf("  action=%d x=%.1f y=%.1f side=%.1f white=%d\n",m.action,m.cx,m.cy,m.side,m.white);
            const size_t expected=mp.manual_page==0 ? (japanese&&set==SET_PS?7:6) : mp.manual_page==1 ? (japanese&&set==SET_PS?2:1) : 2;
            assert(found.size()==expected);
            for(size_t i=0;i<found.size();++i)for(size_t j=i+1;j<found.size();++j)
                assert(std::hypot(found[i].cx-found[j].cx,found[i].cy-found[j].cy)>32);
            auto cleaned=pixels;clear_manual_icons(cleaned,1280,720,found);
            for(int yy=0;yy<720;++yy)for(int xx=0;xx<1280;++xx){
                const size_t k=(size_t)yy*1280+xx;if(!memcmp(&cleaned[k],&pixels[k],sizeof(Rgba)))continue;
                bool inside=false;for(const auto& m:found)if(std::hypot(xx-m.cx,yy-m.cy)<=m.side*.5f+1.01f)inside=true;
                assert(inside);
            }
        }
        assert(on_hd_image(mp.img[set].data(),(uint32_t)mp.img[set].size(),1280,720,1.5f,0,&im));
        assert(im&&im->w==2560&&im->h==1440);
        png(im,(out/("manual_page_"+std::to_string(mp.manual_page)+"_set_"+std::to_string(set)+".png")).wstring());
        release(im);im=nullptr;
    }
    std::puts("[UI-HD TEST] PASS: PS/Xbox/keyboard, 720p-4K/scales, alpha, resource reuse, remapping, source preservation, unrelated/recycled data, opt-out, screen text, map atlas, manual pages.");
    return 0;
}
