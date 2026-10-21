#include "cv_appearance_menu_preview.h"
#include <iterator>
#include <cstdio>
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
namespace cv_appearance {
bool AppearancePreview::open(const std::filesystem::path& directory) {
 if(ready_&&directory_==directory)return true;
 // The locally derived lens bank follows the selected game's language.
 // This is a per-process file overlay; installed game archives stay intact.
 wchar_t executable[32768]{};
 const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
 if(length&&length<32768){
  const auto project=std::filesystem::path(executable).parent_path().parent_path().parent_path();
  const auto expanded=project/"appearance-assets/humanoids";
  _putenv_s("CV_APPEARANCE_ASSET_ROOT",expanded.string().c_str());
  expanded_directory_=expanded/directory.parent_path().filename();
  const auto lens=project/"appearance-assets"/("pl03-"+directory.parent_path().filename().string()+".arc");
  if(std::filesystem::is_regular_file(lens))_putenv_s("CV_WESKER_LENS_ARC",lens.string().c_str());
 }
 PlayerAsset assets[appearance_count];PreviewModel models[appearance_count];std::string error;
 for(unsigned i=0;i<original_appearance_count;i++){
  if(!load_player_asset(directory/(std::string(catalog[i].archive)+".arc"),assets[i],error)||
     (i&&!compatible_player_skin(assets[0],assets[i],error))||
     !make_preview(assets[i],models[i],error)){
   error_=std::string(catalog[i].name)+": "+error;ready_=false;pixels_.clear();return false;
  }
  if(i==4&&!add_preview_glasses(assets[i],models[i],error)){error_=error;return false;}
  std::vector<Resource> hands;
  if(!load_archive_resources(directory/(std::string(catalog[i].hands)+".arc"),hands,error)||
     !add_preview_hands(assets[i],hands,models[i],error)){
   error_=std::string(catalog[i].name)+": "+error;ready_=false;pixels_.clear();return false;
  }
 }
 for(unsigned i=0;i<original_appearance_count;i++){assets_[i]=std::move(assets[i]);models_[i]=std::move(models[i]);}
 directory_=directory;
 selected_=0;ready_=true;error_.clear();pixels_.clear();epoch_=std::chrono::steady_clock::now();last_={};return true;
}
bool AppearancePreview::load(unsigned index){
 if(!ready_||index>=appearance_count)return false;
 if(assets_[index].body_nodes)return true;
 PlayerAsset asset;PreviewModel model;std::string error;
 const auto path=expanded_directory_/(std::string(catalog[index].archive)+".arc");
 if(!load_player_asset(path,asset,error)||!compatible_player_skin(assets_[0],asset,error)||!make_preview(asset,model,error)){
  error_=std::string(catalog[index].name)+": "+error;
  std::fprintf(stderr,"[appearance] %s\n",error_.c_str());return false;
 }
 if(catalog[index].hands){
  std::vector<Resource> hands;
  if(!load_archive_resources(directory_/(std::string(catalog[index].hands)+".arc"),hands,error)||!add_preview_hands(asset,hands,model,error)){
   error_=error;return false;
  }
 }
 assets_[index]=std::move(asset);models_[index]=std::move(model);error_.clear();return true;
}
void AppearancePreview::select(unsigned index) {
 if(index<appearance_count&&selected_!=index){selected_=index;load(index);epoch_=std::chrono::steady_clock::now();pixels_.clear();last_={};}
}
void AppearancePreview::append(std::vector<rsx_ui_quad>& out,float x,float y,float w,float h) {
 if(!ready_||!assets_[selected_].body_nodes)return;
 auto now=std::chrono::steady_clock::now();
 if(pixels_.empty()||x!=px_||y!=py_||w!=pw_||h!=ph_||now-last_>=std::chrono::milliseconds(33)){
  const float angle=float(std::chrono::duration<double>(now-epoch_).count())*.7f;
  pixels_.clear();render_preview(models_[selected_],angle,pixels_,x,y,w,h);
  px_=x;py_=y;pw_=w;ph_=h;last_=now;
 }
 out.insert(out.end(),pixels_.begin(),pixels_.end());
}
}


