#pragma once
// Bounded COPY/literal patches. Reconstruct only a private cache, never game files.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <mutex>
#pragma comment(lib,"bcrypt.lib")
namespace cv_translation_patch {
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
inline bool read(const fs::path& p,Bytes& b){
 std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)return false;
 const auto n=f.tellg();if(n<0||n>256*1024*1024)return false;
 b.resize(size_t(n));f.seekg(0);return b.empty()||bool(f.read((char*)b.data(),b.size()));
}
inline std::string hash(const Bytes& b){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE h=nullptr;unsigned char digest[32];
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 bool ok=BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(h,(PUCHAR)b.data(),ULONG(b.size()),0)>=0&&BCryptFinishHash(h,digest,32,0)>=0;
 if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);if(!ok)return {};
 std::string text;for(auto c:digest){text+="0123456789abcdef"[c>>4];text+="0123456789abcdef"[c&15];}return text;
}
inline unsigned u32(const Bytes& b,size_t at){return unsigned(b[at])|(unsigned(b[at+1])<<8)|(unsigned(b[at+2])<<16)|(unsigned(b[at+3])<<24);}
inline bool apply(const Bytes& original,const Bytes& patch,Bytes& output){
 if(patch.size()<148||std::memcmp(patch.data(),"CVPTBR01",8))return false;
 const unsigned old_size=u32(patch,8),new_size=u32(patch,12),count=u32(patch,16);
 if(old_size!=original.size()||new_size>256*1024*1024||count>1000000||
  hash(original)!=std::string((const char*)patch.data()+20,64))return false;
 output.clear();output.reserve(new_size);size_t at=148;
 for(unsigned i=0;i<count;i++){
  if(at+9>patch.size())return false;unsigned kind=patch[at],off=u32(patch,at+1),n=u32(patch,at+5);at+=9;
  if(!n||n>new_size-output.size())return false;
  if(kind==0){if(off>original.size()||n>original.size()-off)return false;output.insert(output.end(),original.begin()+off,original.begin()+off+n);}
  else if(kind==1){if(off||n>patch.size()-at)return false;output.insert(output.end(),patch.begin()+at,patch.begin()+at+n);at+=n;}
  else return false;
 }
 return at==patch.size()&&output.size()==new_size&&hash(output)==std::string((const char*)patch.data()+84,64);
}
inline fs::path resolve(const fs::path& original,const fs::path& patch_path){
 static std::mutex lock;std::lock_guard<std::mutex> guard(lock);
 Bytes patch;if(!read(patch_path,patch)||patch.size()<148||std::memcmp(patch.data(),"CVPTBR01",8))return {};
 const std::string key((char*)patch.data()+84,64);
 if(key.find_first_not_of("0123456789abcdef")!=std::string::npos)return {};
 wchar_t temp[32768];const DWORD size=GetTempPathW(32768,temp);if(!size||size>=32768)return {};
 auto dest=fs::path(temp)/L"CodeVeronica-PTBR"/(key+".arc");Bytes cached;
 if(read(dest,cached)&&hash(cached)==key)return dest;
 Bytes base,out;if(!read(original,base)||!apply(base,patch,out))return {};
 std::error_code ec;fs::create_directories(dest.parent_path(),ec);if(ec)return {};
 auto part=dest;part+=L"."+std::to_wstring(GetCurrentProcessId())+L".part";
 {std::ofstream f(part,std::ios::binary|std::ios::trunc);if(!f||!f.write((char*)out.data(),out.size()))return {};}
 if(!MoveFileExW(part.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){fs::remove(part,ec);return {};}
 return dest;
}
}
