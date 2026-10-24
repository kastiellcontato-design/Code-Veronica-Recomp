#include "../cv_updater.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstdio>
int main(int argc,char** argv){
 using namespace cv_update;
 for(const char* name:{"Code.Veronica.Recomp.Release.v1.0.zip","CodeVeronica-Recomp-PC-v1.1.zip","Code Veronica Recomp Release v1.2.zip","CodeVeronica-PC-v1.3.zip","VeronicaPort-v2.0.zip"}){
  std::string json=std::string(R"({"tag_name":"v2.0","assets":[{"name":"CodeVeronica-LiveSplit-Speedrun-v1.0.zip","browser_download_url":"bad-livesplit"},{"name":"Veronica-PKG-Extractor-v1.0.zip","browser_download_url":"bad-extractor"},{"name":")")+name+R"(","browser_download_url":"right"},{"name":"source.zip","browser_download_url":"bad-source"}]})";
  Release r;assert(parse_release(json,&r));assert(r.asset_url==L"right");
 }
 for(const char* name:{"CodeVeronica-LiveSplit-Speedrun-v1.0.zip","Veronica-PKG-Extractor-v1.0.zip","CodeVeronica-PC-textures-v1.0.zip","source.zip","Code.Veronica.Recomp.Release.v1.0.zip.sha256","CodeVeronica-PC-v.zip"}){
  const std::string json=std::string(R"({"tag_name":"v2.0","assets":[{"name":")")+name+R"(","browser_download_url":"wrong"}]})";
  Release r;assert(parse_release(json,&r));assert(r.asset_url.empty()&&r.asset_name.empty());
 }
 if(argc>1){std::ifstream f(argv[1]);std::string json((std::istreambuf_iterator<char>(f)),{});Release r;assert(parse_release(json,&r));assert(r.asset_name==L"Code.Veronica.Recomp.Release.v1.0.zip");assert(r.asset_size>0);assert(r.sha256.size()==64);}
 puts("PASS: current/spaced/hyphenated/legacy port names; auxiliary-only release; source/texture/checksum ZIP rejection; actual GitHub release metadata");
}
