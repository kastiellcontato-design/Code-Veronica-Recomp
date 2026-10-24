#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "pkg_core.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
namespace veronica_pkg {namespace {
using B=std::vector<unsigned char>;
void need(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void check(NTSTATUS v){need(v>=0,"Windows encryption operation failed.");}
uint64_t be(const B& b,size_t p,unsigned n){need(p<=b.size()&&n<=b.size()-p,"Truncated package metadata.");uint64_t v=0;while(n--)v=(v<<8)|b[p++];return v;}
uint32_t le(const B& b,size_t p,unsigned n=4){need(p<=b.size()&&n<=b.size()-p,"Truncated PARAM.SFO.");uint32_t v=0;for(unsigned i=0;i<n;i++)v|=uint32_t(b[p+i])<<(i*8);return v;}
std::wstring utf8(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0,"Invalid UTF-8 package filename.");std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);return w;}
std::wstring fold(std::wstring s){for(auto& c:s)c=towupper(c);return s;}
std::filesystem::path safe_name(std::string name){
 need(!name.empty()&&name.size()<4096,"Invalid package filename.");std::replace(name.begin(),name.end(),'\\','/');
 need(name.front()!='/'&&name.back()!='/',"Absolute or empty package path.");
 std::filesystem::path out;size_t start=0;
 while(start<name.size()){size_t end=name.find('/',start);if(end==std::string::npos)end=name.size();std::string part=name.substr(start,end-start);
  need(!part.empty()&&part!="."&&part!=".."&&part.back()!=' '&&part.back()!='.',"Unsafe package path component.");
  for(unsigned char c:part)need(c>=32&&std::string(":*?\"<>|").find(char(c))==std::string::npos,"Unsupported Windows package filename.");
  auto w=utf8(part);need(w.size()<=255,"Package filename is too long.");auto base=fold(w.substr(0,w.find(L'.')));
  need(base!=L"CON"&&base!=L"PRN"&&base!=L"AUX"&&base!=L"NUL"&&!(base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&base[3]>=L'0'&&base[3]<=L'9'),"Reserved Windows package filename.");
  out/=w;start=end+1;
 }return out;
}
void no_links(const std::filesystem::path& path){
 std::filesystem::path current;for(const auto& part:std::filesystem::absolute(path)){current/=part;DWORD a=GetFileAttributesW(current.c_str());need(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Destination contains a link or junction; choose a regular folder.");}
}
struct Cipher {
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE key=nullptr;B object;
 Cipher(){check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0));try{
  check(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_ECB,sizeof(BCRYPT_CHAIN_MODE_ECB),0));DWORD n=0,size=0;check(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof size,&n,0));object.resize(size);
  unsigned char bytes[]={0x2e,0x7b,0x71,0xd7,0xc9,0xc9,0xa1,0x4e,0xa3,0x22,0x1f,0x18,0x88,0x28,0xb8,0xf8};check(BCryptGenerateSymmetricKey(algorithm,&key,object.data(),ULONG(object.size()),bytes,16,0));
 }catch(...){BCryptCloseAlgorithmProvider(algorithm,0);throw;}}
 ~Cipher(){if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
};
struct Entry {std::filesystem::path path;uint64_t offset=0,size=0;bool directory=false;};
struct Package {
 std::ifstream input;uint64_t physical=0,data_offset=0,data_size=0;std::array<unsigned char,16> iv{};Cipher cipher;Info info;std::vector<Entry> entries;
 B raw(uint64_t p,size_t n){need(p<=physical&&n<=physical-p,"PKG is incomplete or truncated.");B b(n);input.clear();input.seekg(std::streamoff(p));if(n)need(bool(input.read((char*)b.data(),n)),"Cannot read PKG file.");return b;}
 B decrypt(uint64_t p,size_t n){
  need(p<=data_size&&n<=data_size-p&&n<=4*1024*1024,"Encrypted PKG range is invalid.");auto data=raw(data_offset+p,n);if(!n)return data;
  uint64_t block=p/16;unsigned skip=unsigned(p%16);size_t length=(skip+n+15)&~size_t(15);B counters(length),stream(length);auto counter=iv;
  uint64_t carry=block;for(int j=15;j>=0;j--){unsigned v=counter[j]+unsigned(carry&255);counter[j]=uint8_t(v);carry=(carry>>8)+(v>>8);}
  for(size_t a=0;a<length;a+=16){std::copy(counter.begin(),counter.end(),counters.begin()+a);for(int j=15;j>=0;j--)if(++counter[j])break;}
  ULONG count=0;check(BCryptEncrypt(cipher.key,counters.data(),ULONG(length),nullptr,nullptr,0,stream.data(),ULONG(length),&count,0));need(count==length,"AES stream length mismatch.");for(size_t i=0;i<n;i++)data[i]^=stream[skip+i];return data;
 }
 std::string sfo(const B& b,const char* wanted){
  need(b.size()>=20&&le(b,0)==0x46535000,"Invalid PARAM.SFO.");size_t keys=le(b,8),values=le(b,12),count=le(b,16);need(count<=4096&&20+count*16<=b.size()&&keys<=b.size()&&values<=b.size(),"Invalid SFO table.");
  for(size_t i=0;i<count;i++){size_t e=20+i*16,k=keys+le(b,e,2);need(k<b.size(),"Invalid SFO key.");size_t end=k;while(end<b.size()&&b[end])++end;need(end<b.size(),"Unterminated SFO key.");if(std::string((const char*)b.data()+k,end-k)!=wanted)continue;
   size_t p=values+le(b,e+12),n=le(b,e+4);need(p<=b.size()&&n<=b.size()-p&&n<4096,"Invalid SFO value.");std::string out((const char*)b.data()+p,n);while(!out.empty()&&!out.back())out.pop_back();return out;
  }return {};
 }
 explicit Package(const std::filesystem::path& path):input(path,std::ios::binary|std::ios::ate){
  need(bool(input),"Cannot open selected PKG.");auto length=input.tellg();need(length>=0xa0,"PKG header is missing.");physical=uint64_t(length);B header=raw(0,0xa0);
  need(be(header,0,4)==0x7f504b47,"This file is not a PS3 PKG.");need(be(header,4,2)==0x8000&&be(header,6,2)==1,"Only retail PS3 game PKGs are supported.");
  need(be(header,0x18,8)==physical,"PKG size does not match its header; download or copy may be incomplete.");
  unsigned count=unsigned(be(header,0x14,4));data_offset=be(header,0x20,8);data_size=be(header,0x28,8);need(count>0&&count<=200000&&data_offset>=0xa0&&data_offset<=physical&&data_size<=physical-data_offset&&uint64_t(count)*32<=data_size,"Invalid PKG data table.");std::copy_n(header.data()+0x70,16,iv.data());
  std::string content((const char*)header.data()+0x30,48);auto nul=content.find('\0');if(nul!=std::string::npos)content.resize(nul);need(content.size()>=16,"Invalid PKG content ID.");info.title_id=content.substr(7,9);
  need(info.title_id=="NPEB00553"||info.title_id=="NPUB30467"||info.title_id=="NPJB00135","This PKG is not one of the three supported Code Veronica releases.");
  std::map<std::wstring,bool> names;
  for(unsigned first=0;first<count;first+=4096){unsigned batch=std::min(4096u,count-first);B table=decrypt(uint64_t(first)*32,size_t(batch)*32);
   for(unsigned j=0;j<batch;j++){size_t a=j*32;uint64_t name_offset=be(table,a,4),name_size=be(table,a+4,4),offset=be(table,a+8,8),size=be(table,a+16,8);auto flags=be(table,a+24,4);unsigned kind=unsigned(flags&255);
    need((flags&0x10000000)==0&&(kind==1||kind==2||kind==3||kind==4||kind==9),"Unsupported PKG file entry type.");need(name_size>0&&name_size<=4096&&name_offset>=uint64_t(count)*32,"Invalid PKG filename range.");B name=decrypt(name_offset,size_t(name_size));std::string text(name.begin(),name.end());auto relative=safe_name(text);bool directory=kind==4;
    need(offset<=data_size&&size<=data_size-offset&&(!directory||size==0),"Invalid PKG file size or offset.");auto key=fold(relative.generic_wstring());need(names.emplace(key,directory).second,"Duplicate or ambiguous filename in PKG.");entries.push_back({relative,offset,size,directory});if(!directory){need(info.bytes<=UINT64_MAX-size,"PKG size overflow.");info.bytes+=size;info.files++;}
   }
  }
  for(const auto& e:entries){auto p=e.path.parent_path();while(!p.empty()){auto it=names.find(fold(p.generic_wstring()));need(it==names.end()||it->second,"PKG file conflicts with a directory.");p=p.parent_path();}}
  auto find=[&](const wchar_t* name)->const Entry*{for(const auto& e:entries)if(fold(e.path.generic_wstring())==name&&!e.directory)return &e;return nullptr;};
  auto s=find(L"PARAM.SFO"),boot=find(L"USRDIR/EBOOT.BIN");need(s&&boot&&s->size<1024*1024&&boot->size>0,"PKG does not contain PARAM.SFO and USRDIR/EBOOT.BIN.");B metadata=decrypt(s->offset,size_t(s->size));need(sfo(metadata,"TITLE_ID")==info.title_id,"PKG and PARAM.SFO title IDs do not match.");info.version=sfo(metadata,"APP_VER");need(info.version=="01.00","Only the base PSN release version 01.00 is supported; this is not a compatible base game.");need(sfo(metadata,"CATEGORY")=="HG","Only the base PSN game is supported, not updates or add-ons.");
 }
};
}
Info inspect(const std::filesystem::path& pkg){return Package(pkg).info;}
std::filesystem::path extract(const std::filesystem::path& pkg,const std::filesystem::path& parent,std::atomic<bool>& cancel,const Notify& notify){
 Package package(pkg);need(!cancel.load(),"Cancelled.");auto root=std::filesystem::absolute(parent).lexically_normal();need(std::filesystem::is_directory(root),"Choose an existing destination folder.");no_links(root);auto target=root/utf8(package.info.title_id);need(!std::filesystem::exists(target),"A folder for this version already exists at the destination. Choose another folder; existing files will not be overwritten.");
 std::error_code ec;auto space=std::filesystem::space(root,ec);need(!ec&&space.available>=package.info.bytes+16*1024*1024,"Insufficient free disk space at destination.");
 auto staging=root/(L".veronica-pkg-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));need(std::filesystem::create_directory(staging),"Cannot create temporary extraction folder.");
 try{Progress progress{0,package.info.bytes,{}};for(const auto& e:package.entries){need(!cancel.load(),"Cancelled.");auto file=staging/e.path;no_links(file);if(e.directory){std::filesystem::create_directories(file);continue;}
   std::filesystem::create_directories(file.parent_path());no_links(file.parent_path());std::ofstream out(file,std::ios::binary|std::ios::trunc);need(bool(out),"Cannot create extracted file.");progress.file=e.path.wstring();
   for(uint64_t pos=0;pos<e.size;){need(!cancel.load(),"Cancelled.");size_t n=size_t(std::min(uint64_t(1024*1024),e.size-pos));B bytes=package.decrypt(e.offset+pos,n);out.write((const char*)bytes.data(),bytes.size());need(bool(out),"Disk write failed during extraction.");pos+=n;progress.done+=n;if(notify)notify(progress);}
   out.close();need(bool(out),"Cannot finish extracted file.");
  }
  need(!cancel.load(),"Cancelled.");no_links(root);need(MoveFileExW(staging.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Cannot finalize extraction; destination may already exist.");return target;
 }catch(...){std::error_code ignored;std::filesystem::remove_all(staging,ignored);throw;}
}
}
