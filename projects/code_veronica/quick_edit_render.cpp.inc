std::wstring quick_key_name(int key){wchar_t name[48]{};LONG scan=LONG(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);if(key>=VK_PRIOR&&key<=VK_DOWN)scan|=1<<24;return key&&GetKeyNameTextW(scan,name,48)?name:L"—";}
std::wstring quick_edit_binding(const cv_quick::Config& c,bool confirm=false){
 const wchar_t* buttons[]={L"×",L"○",L"□",L"△",L"L1",L"R1",L"L2",L"R2",L"L3",L"R3",L"Select",L"Start",L"↑",L"↓",L"←",L"→",L"Home",L"Touch"};
 const int key=confirm?c.confirm_key:c.edit_key,button=confirm?c.confirm_button:c.edit_button;
 std::wstring value=key>0?quick_key_name(key):L"";if(button>=0){if(!value.empty())value+=L" / ";value+=buttons[std::clamp(button,0,17)];}return value;
}
void quick_edit_tip(Graphics& g,const cv_quick::View& menu,int language){
 using namespace quick_edit_text;const auto binding=quick_edit_binding(menu.config,menu.editing);if(binding.empty())return;
 FontFamily family(cv_srt_i18n::font_face(cv_srt_i18n::Language(language)));Font font(&family,11,FontStyleRegular,UnitPixel);SolidBrush ink(Color(230,190,212,196));StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetFormatFlags(StringFormatFlagsNoWrap);
 const auto text=binding+L": "+get(language,menu.editing?Confirm:Edit);g.DrawString(text.c_str(),-1,&font,RectF(51,235,260,18),&format,&ink);
}
void draw_quick_picker(Graphics& g,const cv_quick::View& menu,int language){
 using namespace quick_edit_text;const auto lang=cv_srt_i18n::Language(language);
 FontFamily family(cv_srt_i18n::font_face(lang));Font title(&family,14,FontStyleRegular,UnitPixel),body(&family,12,FontStyleRegular,UnitPixel),caption(&family,10,FontStyleRegular,UnitPixel);
 SolidBrush dark(Color(235,10,16,12)),white(Color(255,215,233,219)),dim(Color(230,150,178,158));Pen edge(Color(160,125,155,134),.8f);RectF panel(51,2,260,230);g.FillRectangle(&dark,panel);g.DrawRectangle(&edge,panel);
 StringFormat text_format;text_format.SetLineAlignment(StringAlignmentCenter);text_format.SetTrimming(StringTrimmingEllipsisCharacter);text_format.SetFormatFlags(StringFormatFlagsNoWrap|(cv_srt_i18n::rtl_text(get(language,Pick))?StringFormatFlagsDirectionRightToLeft:0));
 g.DrawString(get(language,Pick),-1,&title,RectF(63,9,190,27),&text_format,&white);
 StringFormat right;right.SetAlignment(StringAlignmentFar);g.DrawString((std::to_wstring(menu.choice+1)+L"/"+std::to_wstring(menu.choice_count)).c_str(),-1,&caption,RectF(258,16,42,20),&right,&dim);
 const int first=std::clamp(menu.choice-1,0,std::max(0,menu.choice_count-3));
 for(int r=0;r<3;r++){
  const int choice=first+r;if(choice>=menu.choice_count)break;const int entry=menu.choices[choice],id=entry<0?-1:menu.inventory.id(entry);const bool selected=choice==menu.choice;const float y=43+r*49.f;RectF row(62,y,238,45);
  SolidBrush fill(selected?Color(220,75,99,81):Color(185,25,38,29));g.FillRectangle(&fill,row);Pen border(selected?Color(245,209,239,216):Color(100,84,111,92),selected?1.2f:.7f);g.DrawRectangle(&border,row);
  if(entry>=0){if(auto* art=icon(id)){const float fit=std::min(62.f/art->GetWidth(),31.f/art->GetHeight());g.DrawImage(art,RectF(69+(62-art->GetWidth()*fit)/2,y+(45-art->GetHeight()*fit)/2,art->GetWidth()*fit,art->GetHeight()*fit));}}
  const auto name=entry<0?std::wstring(get(language,Empty)):cv_srt_i18n::item(id,lang);
  g.DrawString(name.c_str(),-1,&body,RectF(139,y+1,153,43),&text_format,selected?&white:&dim);
 }
 const auto directions=quick_key_name(menu.config.keys[0])+L" / "+quick_key_name(menu.config.keys[2])+L"  ·  "+get(language,Cancel);
 g.DrawString(directions.c_str(),-1,&caption,RectF(63,198,237,25),&text_format,&dim);
}
