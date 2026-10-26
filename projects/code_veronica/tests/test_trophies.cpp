#include "../cv_trophies.cpp"
#include <cassert>
#include <iostream>
extern "C" {void trophy_test_open(const char*);int trophy_test_unlock(int);void trophy_test_state(unsigned,unsigned);void trophy_test_close(void);}
static unsigned notices=0;
namespace cv_trophy_popup {void initialize(){} void enabled(bool){} void enqueue(const cv_trophies::Trophy& trophy,int){assert(!trophy.name.empty()&&!trophy.icon.empty());notices++;}}
static std::string bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 using namespace cv_trophies;assert(argc==3);const fs::path game=fs::u8path(argv[1]),fixture=fs::u8path(argv[2]);
 assert(!fs::exists(fixture));fs::create_directories(fixture);storage_for_test(fixture/L"trophies");
 for(int lang=0;lang<13;lang++){
  auto s=view(game,lang,true);assert(s.catalog&&s.catalog->items.size()==12&&s.error.empty());
  unsigned grades[5]={};for(const auto& t:s.catalog->items){grades[t.grade]++;assert(!t.name.empty()&&!t.description.empty()&&t.icon.size()>1000);}
  assert(!grades[1]&&grades[2]==1&&grades[3]==2&&grades[4]==9);
  for(auto* text:cv_trophy_text::strings[lang])assert(text&&*text);
 }
 start(game,1,true,true);auto s=view(game,1);const std::string id=s.catalog->commid;const auto file=fixture/L"trophies"/fs::u8path(id+".json");
 trophy_test_open(id.c_str());trophy_test_state(12,0);
 assert(trophy_test_unlock(0)==0);assert(notices==1);trophy_test_state(12,1);assert(fs::exists(file));
 assert(uint32_t(trophy_test_unlock(0))==0x80022915u&&notices==1);assert(uint32_t(trophy_test_unlock(12))!=0);
 options(false,true,1);assert(trophy_test_unlock(1)==0);trophy_test_state(12,1);assert(notices==1);
 options(true,false,1);assert(trophy_test_unlock(1)==0);trophy_test_state(12,3);assert(notices==1);trophy_test_close();
 auto saved=bytes(file);storage_for_test(fixture/L"trophies");start(game,1,true,true);trophy_test_open(id.c_str());trophy_test_state(12,3);trophy_test_close();assert(bytes(file)==saved);
 const auto save=fixture/L"savedata/CodeVeronicaLibrary/test.cvsav";fs::create_directories(save.parent_path());std::ofstream(save)<<"untouched-save";
 assert(reset());assert(bytes(save)=="untouched-save");assert(!has_unlocked());unsigned backups=0;for(auto& entry:fs::directory_iterator(file.parent_path()))if(entry.path().extension()==L".bak"){backups++;assert(bytes(entry.path())==saved);}assert(backups==1);
 // A truncated/corrupt progress file must never be replaced with empty data.
 std::ofstream(file)<<"{\"trophies\":[{\"id\":0,\"timestamp\":4},garbage]}";auto corrupt=bytes(file);view(game,1,true);assert(!reset());assert(bytes(file)==corrupt);uint64_t stamp=0;assert(g_sce_np_trophy_host->unlock(id.c_str(),1,&stamp)==4&&bytes(file)==corrupt);
 // Accept legacy JSON only when canonical data does not exist; reset wins.
 fs::remove(file);std::ofstream(file.parent_path()/fs::u8path(id.substr(0,9)+".json"))<<"{\"trophies\":[{\"id\":0,\"timestamp\":123}]}";assert(view(game,1,true).unlocked[0]==123);assert(reset());assert(!view(game,1,true).unlocked[0]);
 // Bounds/XML failures are rejected before any user data is written.
 std::array<uint64_t,128> values{};std::ofstream(file)<<"{\"trophies\": [{\"id\":129,\"timestamp\":4}]}";assert(!read_progress(file,values));
 auto bad=fixture/L"invalid/TROPDIR/NPWR00000_00";fs::create_directories(bad);std::ofstream(bad/L"TROPHY.TRP")<<"not a TRP";assert(!view(fixture/L"invalid",1,true).catalog);
 std::string invalid="<!DOCTYPE a [<!ENTITY e SYSTEM 'file:///x'>]><trophyconf>&e;</trophyconf>";Catalog c;assert(!xml_catalog(std::vector<unsigned char>(invalid.begin(),invalid.end()),c));
 puts("PASS: 13 languages; real 12-trophy catalog/icons; guest ABI/BE/callbacks; unlock/duplicate/disabled/notifications; reload; reset backup; saves unchanged; corrupt data protected; legacy migration; invalid archive/XML rejected.");
 return 0;
}
