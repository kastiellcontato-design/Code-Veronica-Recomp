// Launcher-only presentation; the catalog and local progress live in cv_trophies.
namespace tt=cv_trophy_text;
int trophy_filter=0;
std::shared_ptr<const cv_trophies::Catalog> trophy_images_catalog;
std::map<int,std::unique_ptr<Bitmap>> trophy_images;
const wchar_t* trophy_text(tt::Id id){return tt::get(g_lang,id);}
cv_trophies::Snapshot trophy_view(bool refresh=false){return cv_trophies::view(fs::path(g.s.game),g_lang,refresh);}
void trophy_wrap(Graphics& gr,const std::wstring& content,const RectF& r,const Font& font,Color color){
    StringFormat sf;sf.SetLineAlignment(StringAlignmentNear);sf.SetTrimming(StringTrimmingEllipsisWord);
    if(g_lang==LANG_AR)sf.SetFormatFlags(StringFormatFlagsDirectionRightToLeft);
    SolidBrush brush(color);gr.DrawString(content.c_str(),-1,&font,r,&sf,&brush);
}
Bitmap* trophy_image(const cv_trophies::Trophy& item){
    auto it=trophy_images.find(item.id);if(it!=trophy_images.end())return it->second.get();
    std::unique_ptr<Bitmap> image;
    if(!item.icon.empty()){
        HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,item.icon.size());
        if(block){void* p=GlobalLock(block);if(p){memcpy(p,item.icon.data(),item.icon.size());GlobalUnlock(block);IStream* input=nullptr;
            if(SUCCEEDED(CreateStreamOnHGlobal(block,TRUE,&input))){Bitmap decoded(input);if(decoded.GetLastStatus()==Ok)image.reset(decoded.Clone(0,0,INT(decoded.GetWidth()),INT(decoded.GetHeight()),PixelFormat32bppARGB));input->Release();}else GlobalFree(block);
        }else GlobalFree(block);}
    }
    auto* result=image.get();trophy_images.emplace(item.id,std::move(image));return result;
}
void draw_trophies(Graphics& gr,const Font& head,const Font& body,const Font& fsm){
    const float x=SIDE+32.f,w=W-x-32.f;float y=30;
    text(gr,trophy_text(tt::Title),x,y,head,C_TEXT,StringAlignmentNear,w,30);y+=38;
    trophy_wrap(gr,trophy_text(tt::Intro),RectF(x,y,w,36),fsm,C_DIM);y+=44;
    const float gap=16,col=(w-gap)/2;
    option_card(gr,x,y,col,92,g.s.trophies,ID_TROPHY_ENABLE,trophy_text(tt::Enable),trophy_text(tt::EnableHelp),body,fsm);
    option_card(gr,x+col+gap,y,col,92,g.s.trophy_notifications,ID_TROPHY_NOTIFY,trophy_text(tt::Notify),trophy_text(tt::NotifyHelp),body,fsm);y+=108;
    text(gr,trophy_text(tt::Position),x,y,body,C_TEXT,StringAlignmentNear,w-340,32);
    dropdown(gr,RectF(x+w-320,y,320,32),T(S_SRT_TL+g.s.trophy_corner),ID_DD_TROPHY_CORNER,body);y+=50;
    auto snapshot=trophy_view();
    if(snapshot.catalog!=trophy_images_catalog){trophy_images.clear();trophy_images_catalog=snapshot.catalog;}
    unsigned total=snapshot.catalog?unsigned(snapshot.catalog->items.size()):0,unlocked=0;
    if(snapshot.catalog)for(auto& item:snapshot.catalog->items)unlocked+=snapshot.unlocked[item.id]!=0;
    wchar_t progress[128];swprintf(progress,128,trophy_text(tt::Progress),unlocked,total);
    text(gr,progress,x,y,body,C_TEXT,StringAlignmentNear,w,26);y+=34;
    dropdown(gr,RectF(x,y,210,32),trophy_text(tt::Id(tt::All+trophy_filter)),ID_DD_TROPHY_FILTER,body);
    button(gr,RectF(x+224,y,260,32),trophy_text(tt::Reset),ID_TROPHY_RESET,body,false,!g.running&&snapshot.catalog&&!snapshot.error.size());
    button(gr,RectF(x+498,y,160,32),trophy_text(tt::Refresh),ID_TROPHY_REFRESH,body);y+=48;
    trophy_wrap(gr,trophy_text(tt::LocalNote),RectF(x,y,w,42),fsm,C_DIM);y+=50;
    if(!snapshot.error.empty()){
        trophy_wrap(gr,trophy_text(snapshot.catalog?tt::Error:tt::Missing),RectF(x,y,w,60),body,C_DIM);y+=68;
    }
    unsigned shown=0;
    if(snapshot.catalog)for(const auto& item:snapshot.catalog->items){
        bool earned=snapshot.unlocked[item.id]!=0;if((trophy_filter==1&&!earned)||(trophy_filter==2&&earned))continue;shown++;
        RectF card(x,y,w,116);SolidBrush bg(C_PANEL);Pen border(earned?Color(255,117,94,55):C_FIELD_BD);gr.FillRectangle(&bg,card);gr.DrawRectangle(&border,card);
        if(auto* img=trophy_image(item)){
            if(earned)gr.DrawImage(img,RectF(x+14,y+18,76,76));else{
                ColorMatrix matrix={{{.23f,.23f,.23f,0,0},{.23f,.23f,.23f,0,0},{.23f,.23f,.23f,0,0},{0,0,0,.45f,0},{0,0,0,0,1}}};
                ImageAttributes attrs;attrs.SetColorMatrix(&matrix);gr.DrawImage(img,RectF(x+14,y+18,76,76),0,0,REAL(img->GetWidth()),REAL(img->GetHeight()),UnitPixel,&attrs);
            }
        }else nav_icon(gr,P_TROPHIES,x+40,y+40,C_DIM);
        const auto nameColor=earned?C_TEXT:Color(255,156,144,128),detailColor=earned?C_DIM:Color(255,132,122,110);
        text(gr,item.name,x+106,y+10,body,nameColor,StringAlignmentNear,w-330,26);
        trophy_wrap(gr,item.description,RectF(x+106,y+40,w-330,42),fsm,detailColor);
        text(gr,L"ID "+std::to_wstring(item.id)+L" · "+trophy_text(item.grade==1?tt::Platinum:item.grade==2?tt::Gold:item.grade==3?tt::Silver:tt::Bronze),x+106,y+87,fsm,detailColor,StringAlignmentNear,w-330,20);
        text(gr,trophy_text(earned?tt::StatusUnlocked:tt::StatusLocked),x+w-210,y+43,fsm,earned?C_OK:C_DIM,StringAlignmentFar,192,28);
        y+=128;
    }
    if(snapshot.catalog&&!shown){trophy_wrap(gr,trophy_text(tt::Empty),RectF(x,y,w,45),body,C_DIM);y+=54;}
    g_page_end=std::max(g_page_end,y);
}
