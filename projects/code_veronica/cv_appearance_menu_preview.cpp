#include "cv_appearance_menu_preview.h"
#include "cv_appearance_prepare.h"
#include <iterator>
#include <cstdio>
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <exception>
namespace cv_appearance {
bool AppearancePreview::open(const std::filesystem::path& directory) {
 if(ready_&&directory_==directory)return true;
 ready_=false;directory_=directory;error_.clear();pixels_.clear();
 for(unsigned i=0;i<appearance_count;i++){assets_[i]={};models_[i]={};}
 try {
  cache_root_=appearance_cache_root(directory);
  _putenv_s("CV_APPEARANCE_ASSET_ROOT",cache_root_.string().c_str());
  _putenv_s("CV_WESKER_LENS_ARC","");
 } catch(const std::exception& e){error_=e.what();return false;}
 selected_=0;ready_=true;
 // Only the baseline is required to open the menu. Other entries are loaded
 // independently on selection, so an optional asset cannot block every skin.
 if(!load(0)){ready_=false;return false;}
 epoch_=std::chrono::steady_clock::now();last_={};return true;
}
bool AppearancePreview::load(unsigned index){
 if(!ready_||index>=appearance_count)return false;
 if(assets_[index].body_nodes){error_.clear();return true;}
 PlayerAsset asset;PreviewModel model;std::string error;std::filesystem::path path;
 if(!prepare_appearance_asset(directory_,cache_root_,index,path,error)){
  error_=std::string(catalog[index].name)+": "+error;
  std::fprintf(stderr,"[appearance] %s\n",error_.c_str());return false;
 }
 if(index==4)_putenv_s("CV_WESKER_LENS_ARC",path.string().c_str());
 if(!load_player_asset(path,asset,error)||
    (index&&!compatible_player_skin(assets_[0],asset,error))||!make_preview(asset,model,error)||
    (index==4&&!add_preview_glasses(asset,model,error))){
  error_=std::string(catalog[index].name)+": "+error;
  std::fprintf(stderr,"[appearance] %s\n",error_.c_str());return false;
 }
 if(catalog[index].hands){
  std::vector<Resource> hands;
  if(!load_archive_resources(directory_/(std::string(catalog[index].hands)+".arc"),hands,error)||!add_preview_hands(asset,hands,model,error)){
   error_=std::string(catalog[index].name)+": "+error;return false;
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


