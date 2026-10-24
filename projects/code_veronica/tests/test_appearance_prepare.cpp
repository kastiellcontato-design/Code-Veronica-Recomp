#include "cv_appearance_prepare.h"
#include "cv_appearance_assets.h"
#include "cv_appearance_catalog.h"
#include "cv_appearance_menu_preview.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
int wmain(int argc,wchar_t** argv){
 try{
  if(argc!=3)throw std::runtime_error("Usage: test <system> <temporary-cache>");
  std::filesystem::path system=argv[1],cache=argv[2];std::string error;
  auto game_root=system;for(unsigned i=0;i<6;i++)game_root=game_root.parent_path();
  auto detected=cv_appearance::appearance_system_directory(game_root);
  if(!std::filesystem::is_regular_file(detected/"pl00.arc"))throw std::runtime_error("Regional system detection failed");
  cv_appearance::PlayerAsset baseline;
  if(!cv_appearance::load_player_asset(system/"pl00.arc",baseline,error))throw std::runtime_error(error);
  for(unsigned i=0;i<cv_appearance::appearance_count;i++){
   std::filesystem::path file;
   if(!cv_appearance::prepare_appearance_asset(system,cache,i,file,error))throw std::runtime_error(std::string(cv_appearance::catalog[i].name)+": "+error);
   cv_appearance::PlayerAsset asset;cv_appearance::PreviewModel preview;
   if(!cv_appearance::load_player_asset(file,asset,error)||!cv_appearance::compatible_player_skin(baseline,asset,error)||!cv_appearance::make_preview(asset,preview,error))throw std::runtime_error(std::string(cv_appearance::catalog[i].name)+": "+error);
   if(i==4&&!cv_appearance::add_preview_glasses(asset,preview,error))throw std::runtime_error(error);
   if(cv_appearance::catalog[i].hands){std::vector<cv_appearance::Resource> hands;if(!cv_appearance::load_archive_resources(system/(std::string(cv_appearance::catalog[i].hands)+".arc"),hands,error)||!cv_appearance::add_preview_hands(asset,hands,preview,error))throw std::runtime_error(error);}
   if(preview.triangles.empty()||preview.textures.empty())throw std::runtime_error("Empty preview");
   if(i>=4){auto time=std::filesystem::last_write_time(file);std::filesystem::path again;if(!cv_appearance::prepare_appearance_asset(system,cache,i,again,error)||again!=file||time!=std::filesystem::last_write_time(file))throw std::runtime_error("Cache reuse failed");}
   std::cout<<"PASS "<<i<<" "<<preview.triangles.size()<<" triangles "<<preview.textures.size()<<" textures"<<std::endl;
  }
  // A truncated cached archive is rebuilt from the game, never trusted.
  std::filesystem::path file;
  if(!cv_appearance::prepare_appearance_asset(system,cache,5,file,error))throw std::runtime_error(error);
  {std::ofstream f(file,std::ios::trunc);f<<"broken";}
  if(!cv_appearance::prepare_appearance_asset(system,cache,5,file,error)||std::filesystem::file_size(file)<100)throw std::runtime_error("Cache repair failed");
  // An absent optional source must return an error without touching originals.
  if(cv_appearance::prepare_appearance_asset(cache/"missing/system",cache/"missing-cache",5,file,error)||error.empty())throw std::runtime_error("Missing source not rejected");
  // Exercise the real menu preview loader with an absent optional character.
  auto fixture=cache/"isolated-source/jpn/system";
  std::filesystem::create_directories(fixture);
  for(const char* name:{"pl00.arc","pl00w00.arc"})std::filesystem::copy_file(system/name,fixture/name,std::filesystem::copy_options::overwrite_existing);
  cv_appearance::AppearancePreview menu;
  if(!menu.open(fixture)||!menu.asset(0))throw std::runtime_error("Independent baseline menu load failed");
  menu.select(4);
  if(menu.asset(4)||menu.error().empty())throw std::runtime_error("Missing optional preview not rejected");
  menu.select(0);
  if(!menu.asset(0)||!menu.error().empty())throw std::runtime_error("Optional failure blocked baseline preview");
  std::cout<<"PASS cache repair and missing-source handling; game not executed"<<std::endl;
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
