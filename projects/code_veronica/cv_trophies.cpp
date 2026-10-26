#define NOMINMAX
#include "cv_trophies.h"
#include "cv_trophy_popup.h"
#include "cv_user_data.h"
#include "cv_trophy_i18n.h"
#include "cv_trophy_translations.h"
#include "../../libs/network/sceNpTrophyHost.h"
#include <windows.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <mutex>
#include <fstream>
#include <map>
#include <set>
#include <regex>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace cv_trophies {
namespace fs=std::filesystem;using Microsoft::WRL::ComPtr;
namespace {
std::mutex lock;std::shared_ptr<Catalog> catalog;std::array<uint64_t,128> progress{};
fs::path selected_game,storage_override;int selected_lang=-1;bool enabled=true,notifications=true,store_valid=true;
std::wstring error;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)return {};std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);return w;}
std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
uint32_t be32(const unsigned char* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint64_t be64(const unsigned char* p){return uint64_t(be32(p))<<32|be32(p+4);}
bool safe_id(const std::string& s){if(s.size()!=12||s.substr(0,4)!="NPWR"||s[9]!='_')return false;for(size_t i=4;i<s.size();i++)if(i!=9&&(s[i]<'0'||s[i]>'9'))return false;return true;}
std::vector<unsigned char> read_file(const fs::path& path,size_t limit){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()<0||uint64_t(f.tellg())>limit)return {};auto b=std::vector<unsigned char>(static_cast<size_t>(f.tellg()));f.seekg(0);if(!b.empty()&&!f.read((char*)b.data(),b.size()))return {};return b;}
ComPtr<IStream> stream(const std::vector<unsigned char>& data){ComPtr<IStream> result;if(data.empty())return result;HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,data.size());if(!mem)return result;void* p=GlobalLock(mem);if(!p){GlobalFree(mem);return result;}memcpy(p,data.data(),data.size());GlobalUnlock(mem);if(FAILED(CreateStreamOnHGlobal(mem,TRUE,&result)))GlobalFree(mem);return result;}
struct Archive {std::map<std::string,std::vector<unsigned char>> entries;};
bool load_trp(const fs::path& path,Archive& result){
 auto b=read_file(path,64u<<20);if(b.size()<64||be32(b.data())!=0xdca24d00||be32(b.data()+4)>3)return false;
 uint64_t length=be64(b.data()+8);uint32_t count=be32(b.data()+16),step=be32(b.data()+20);
 if(length!=b.size()||!count||count>512||step!=64||64ull+uint64_t(count)*step>length)return false;
 for(unsigned i=0;i<count;i++){const auto* e=b.data()+64+i*64;size_t n=0;while(n<32&&e[n])n++;if(!n||n==32)return false;
  std::string name((char*)e,n);if(name.find_first_of("/\\:")!=std::string::npos||name.find("..")!=std::string::npos||result.entries.count(name))return false;
  uint64_t at=be64(e+32),size=be64(e+40);if(at<64ull+count*64ull||at>length||size>length-at||size>(16u<<20))return false;
  result.entries.emplace(name,std::vector<unsigned char>(b.begin()+size_t(at),b.begin()+size_t(at+size)));
 }return true;
}
std::wstring attribute(IXmlReader* reader,const wchar_t* key){std::wstring out;if(SUCCEEDED(reader->MoveToAttributeByName(key,nullptr))){const wchar_t* text=nullptr;UINT n=0;if(SUCCEEDED(reader->GetValue(&text,&n)))out.assign(text,n);reader->MoveToElement();}return out;}
bool xml_catalog(const std::vector<unsigned char>& data,Catalog& out){
 auto input=stream(data);ComPtr<IXmlReader> reader;if(!input||FAILED(CreateXmlReader(__uuidof(IXmlReader),(void**)reader.GetAddressOf(),nullptr)))return false;
 reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);reader->SetProperty(XmlReaderProperty_MaxElementDepth,16);if(FAILED(reader->SetInput(input.Get())))return false;
 XmlNodeType node;Trophy item;bool in_trophy=false;std::wstring tag;std::set<int> ids;HRESULT hr;
 while((hr=reader->Read(&node))==S_OK){const wchar_t* text=nullptr;UINT n=0;
  if(node==XmlNodeType_Element){reader->GetLocalName(&text,&n);tag.assign(text,n);
   if(tag==L"trophy"){
    if(in_trophy)return false;in_trophy=true;item=Trophy{};
    auto id=attribute(reader.Get(),L"id");if(id.empty()||id.size()>3||id.find_first_not_of(L"0123456789")!=std::wstring::npos)return false;item.id=std::stoi(id);if(item.id>=128||!ids.insert(item.id).second)return false;
    auto grade=attribute(reader.Get(),L"ttype");item.grade=grade==L"P"?1:grade==L"G"?2:grade==L"S"?3:grade==L"B"?4:0;if(!item.grade)return false;item.hidden=attribute(reader.Get(),L"hidden")==L"yes";
   }
  }else if(node==XmlNodeType_Text||node==XmlNodeType_CDATA){reader->GetValue(&text,&n);std::wstring value(text,n);
   if(in_trophy&&tag==L"name")item.name+=value;else if(in_trophy&&tag==L"detail")item.description+=value;else if(!in_trophy&&tag==L"npcommid")out.commid+=utf8(value);else if(!in_trophy&&tag==L"title-name")out.title+=value;
  }else if(node==XmlNodeType_EndElement){reader->GetLocalName(&text,&n);if(std::wstring(text,n)==L"trophy"){if(!in_trophy||item.name.empty()||item.description.empty())return false;out.items.push_back(std::move(item));in_trophy=false;}tag.clear();}
 }
 if(hr!=S_FALSE||in_trophy||!safe_id(out.commid)||out.items.empty()||out.items.size()>128)return false;
 std::sort(out.items.begin(),out.items.end(),[](const auto& a,const auto& b){return a.id<b.id;});return true;
}
std::shared_ptr<Catalog> parse(const fs::path& game,int language){
 std::error_code ec;const auto root=fs::is_directory(game/L"TROPDIR",ec)?game/L"TROPDIR":game/L"PS3_GAME/TROPDIR";ec.clear();
 std::vector<fs::path> packs;for(fs::directory_iterator it(root,ec),end;!ec&&it!=end;it.increment(ec))if(it->is_directory(ec)&&fs::is_regular_file(it->path()/L"TROPHY.TRP",ec))packs.push_back(it->path()/L"TROPHY.TRP");
 std::sort(packs.begin(),packs.end());
 for(const auto& path:packs){Archive archive;if(!load_trp(path,archive))continue;auto result=std::make_shared<Catalog>();
  int locale=language==3?0:language==4?2:language==2?3:language==5?4:language==6?5:1;
  char name[32];snprintf(name,sizeof name,"TROP_%02d.SFM",locale);auto it=archive.entries.find(name);if(it==archive.entries.end())it=archive.entries.find("TROP.SFM");if(it==archive.entries.end()||!xml_catalog(it->second,*result))continue;
  // Curated extra languages apply only to the known CVX HD set, never to an
  // arbitrary TRP whose IDs might have completely different meanings.
  if(result->items.size()==12){auto english=archive.entries.find("TROP_01.SFM");Catalog reference;
   if(english!=archive.entries.end()&&xml_catalog(english->second,reference)&&reference.items.size()==12&&reference.items[0].name==L"The Terror Begins"&&reference.items[11].name==L"From The Young Lady")
    for(auto& item:result->items)for(const auto& tr:cv_trophy_translation::entries)if(tr.language==language&&tr.id==item.id){item.name=tr.name;item.description=tr.detail;}
  }
  for(auto& item:result->items){snprintf(name,sizeof name,"TROP%03d.PNG",item.id);auto image=archive.entries.find(name);if(image!=archive.entries.end())item.icon=std::move(image->second);}
  return result;
 }return {};
}
fs::path directory(){return storage_override.empty()?cv_user_data::trophies():storage_override;}
fs::path progress_file(){return directory()/fs::u8path(catalog->commid+".json");}
bool read_progress(const fs::path& file,std::array<uint64_t,128>& values){
 auto data=read_file(file,1u<<20);if(data.empty())return false;std::string text(data.begin(),data.end());
 static const std::regex document(R"(^\s*\{\s*(?:"version"\s*:\s*1\s*,\s*)?"trophies"\s*:\s*\[([\s\S]*)\]\s*\}\s*$)");
 std::smatch match;if(!std::regex_match(text,match,document))return false;
 text=match[1];
 static const std::regex entry(R"(\{\s*"id"\s*:\s*([0-9]+)\s*,\s*"timestamp"\s*:\s*([0-9]+)\s*\})");
 size_t at=0;bool first=true;std::set<unsigned long> seen;
 try{for(auto it=std::sregex_iterator(text.begin(),text.end(),entry);it!=std::sregex_iterator();++it){auto gap=text.substr(at,size_t(it->position())-at);static const std::regex white(R"(^\s*$)"),comma(R"(^\s*,\s*$)");if(!std::regex_match(gap,first?white:comma))return false;first=false;auto id=std::stoul((*it)[1]);auto stamp=std::stoull((*it)[2]);if(id>=128||!seen.insert(id).second)return false;values[id]=std::max(values[id],stamp?uint64_t(stamp):uint64_t(1));at=size_t(it->position()+it->length());}if(text.substr(at).find_first_not_of(" \r\n\t")!=std::string::npos)return false;}catch(...){return false;}
 return true;
}
bool load_progress(){
 progress.fill(0);store_valid=true;if(!catalog)return true;std::error_code ec;
 const auto path=progress_file();if(fs::exists(path,ec)){store_valid=read_progress(path,progress);return store_valid;}
 // Import legacy HLE filenames and pre-bin layouts only when no canonical
 // file exists. A deliberate reset must not resurrect those older records.
 std::vector<fs::path> dirs={directory()};if(storage_override.empty()){auto root=cv_user_data::installation();dirs.push_back(root/L"gamedata/trophies");dirs.push_back(root/L"bin/gamedata/trophies");}
 for(const auto& dir:dirs)for(const auto& id:{catalog->commid,catalog->commid.substr(0,9)}){auto old=dir/fs::u8path(id+".json");if(fs::exists(old,ec)&&!read_progress(old,progress)){store_valid=false;return false;}}
 return true;
}
struct WriterLock {HANDLE mutex=nullptr;bool held=false;WriterLock(){auto path=fs::absolute(progress_file()).lexically_normal().wstring();uint64_t hash=1469598103934665603ull;for(wchar_t c:path){hash^=towlower(c);hash*=1099511628211ull;}wchar_t name[96];swprintf(name,96,L"Local\\CVTrophy-%016llx",hash);mutex=CreateMutexW(nullptr,FALSE,name);if(mutex){DWORD r=WaitForSingleObject(mutex,1000);held=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}}~WriterLock(){if(held)ReleaseMutex(mutex);if(mutex)CloseHandle(mutex);}};
bool write_progress(){
 std::error_code ec;fs::create_directories(directory(),ec);if(ec)return false;
 std::string data="{\n  \"version\": 1,\n  \"trophies\": [";bool first=true;
 for(unsigned i=0;i<128;i++)if(progress[i]){data+=first?"\n":",\n";first=false;data+="    { \"id\": "+std::to_string(i)+", \"timestamp\": "+std::to_string(progress[i])+" }";}data+="\n  ]\n}\n";
 auto target=progress_file(),temp=target;temp+=L".pending-"+std::to_wstring(GetCurrentProcessId());HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 DWORD written=0;bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);
 if(ok)ok=MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok)DeleteFileW(temp.c_str());return ok;
}
void ensure(const fs::path& game,int language,bool refresh){
 language=std::clamp(language,0,12);
 if(game!=selected_game||language!=selected_lang||refresh){selected_game=game;selected_lang=language;error.clear();
  try{catalog=parse(game,language);}catch(const std::exception&){catalog.reset();}
  if(!catalog)error=L"catalog-unavailable";if(!load_progress())error=L"progress-read-failed";
 }
}
const Trophy* find(int id){if(catalog)for(const auto& item:catalog->items)if(item.id==id)return &item;return nullptr;}
int query(const char* commid,int id,SceNpTrophyHostGame* game,SceNpTrophyHostItem* item){
 std::lock_guard<std::mutex> guard(lock);if(!catalog||catalog->commid!=commid)return 0;
 if(game){memset(game,0,sizeof(*game));game->count=uint32_t(catalog->items.size());for(auto& t:catalog->items)game->grades[t.grade]++;auto title=utf8(catalog->title);strncpy_s(game->title,title.c_str(),_TRUNCATE);strncpy_s(game->description,title.c_str(),_TRUNCATE);}
 if(item){auto t=find(id);if(!t)return 0;memset(item,0,sizeof(*item));item->grade=t->grade;item->hidden=t->hidden;item->timestamp=progress[id];strncpy_s(item->name,utf8(t->name).c_str(),_TRUNCATE);strncpy_s(item->description,utf8(t->description).c_str(),_TRUNCATE);}
 return 1;
}
int unlock(const char* commid,int id,uint64_t* timestamp){
 Trophy notice;bool show_notice=false;int notice_language=1;int status=0;
 {std::lock_guard<std::mutex> guard(lock);if(!enabled||!catalog)return 2;if(catalog->commid!=commid||!find(id))return 3;
  WriterLock file;if(!file.held||!load_progress())return 4;if(progress[id]){if(timestamp)*timestamp=progress[id];return 1;}
  auto before=progress;progress[id]=uint64_t(std::time(nullptr));if(!write_progress()){progress=before;error=L"progress-write-failed";return 4;}if(timestamp)*timestamp=progress[id];
  if(notifications){notice=*find(id);notice_language=selected_lang;show_notice=true;}
  fprintf(stderr,"[trophies] unlocked id=%d set=%s (local progress committed)\n",id,commid);
 }
 if(show_notice)cv_trophy_popup::enqueue(notice,notice_language);return status;
}
const SceNpTrophyHostBackend backend={query,unlock};
}
Snapshot view(const fs::path& game,int language,bool refresh){std::lock_guard<std::mutex> guard(lock);ensure(game,language,refresh);return {catalog,progress,error,enabled,notifications};}
void options(bool on,bool notices,int language){std::lock_guard<std::mutex> guard(lock);enabled=on;notifications=notices;cv_trophy_popup::enabled(on&&notices);if(catalog&&language!=selected_lang)ensure(selected_game,language,false);}
void start(const fs::path& game,int language,bool on,bool notices){std::lock_guard<std::mutex> guard(lock);enabled=on;notifications=notices;cv_trophy_popup::initialize();cv_trophy_popup::enabled(on&&notices);ensure(game,language,true);g_sce_np_trophy_host=&backend;fprintf(stderr,"[trophies] local backend %s, notification=%d, catalog=%zu\n",enabled?"enabled":"disabled",notices,catalog?catalog->items.size():0);}
bool has_unlocked(){std::lock_guard<std::mutex> guard(lock);return std::any_of(progress.begin(),progress.end(),[](auto t){return t!=0;});}
bool reset(){std::lock_guard<std::mutex> guard(lock);if(!catalog)return false;WriterLock file;if(!file.held||!load_progress())return false;
 auto original=progress;const auto target=progress_file();std::error_code ec;
 if(fs::exists(target,ec)){auto backup=target;backup+=L".reset-"+std::to_wstring(GetTickCount64())+L".bak";if(!CopyFileW(target.c_str(),backup.c_str(),TRUE))return false;}
 else if(std::any_of(progress.begin(),progress.end(),[](auto t){return t!=0;})){auto backup=target;backup+=L".legacy-reset-backup.json";if(!write_progress())return false;if(!CopyFileW(target.c_str(),backup.c_str(),TRUE))return false;}
 progress.fill(0);if(!write_progress()){progress=original;return false;}error.clear();return true;
}
void storage_for_test(const fs::path& path){std::lock_guard<std::mutex> guard(lock);storage_override=path;selected_game.clear();selected_lang=-1;catalog.reset();progress.fill(0);}
}
