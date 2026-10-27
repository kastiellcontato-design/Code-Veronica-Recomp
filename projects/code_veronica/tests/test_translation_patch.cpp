#include "../cv_translation_patch.h"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
 using namespace cv_translation_patch;
 assert(argc==4);auto root=fs::path(argv[1]),patches=fs::path(argv[2]),expected=fs::path(argv[3]);unsigned count=0;
 for(auto title:{"NPEB00553","NPUB30467"})for(auto& file:fs::recursive_directory_iterator(patches/title)){
  if(file.path().extension()!=L".cvp")continue;
  auto rel=fs::relative(file.path(),patches/title);rel.replace_extension();
  auto original=root/title/L"USRDIR/BHCV/nativePS3/biocv_disc"/(std::string(title)=="NPEB00553"?"eng":"usa")/rel;
  Bytes base,patch,result,wanted;assert(read(original,base)&&read(file.path(),patch)&&read(expected/title/rel,wanted));
  assert(apply(base,patch,result)&&result==wanted);
  if(count==0){
   auto cached=resolve(original,file.path());Bytes got;assert(!cached.empty()&&read(cached,got)&&got==wanted);
   assert(resolve(original,file.path())==cached);
   auto corrupt=patch;corrupt.back()^=1;assert(!apply(base,corrupt,result));
   corrupt=patch;corrupt[16]=255;corrupt[17]=255;corrupt[18]=255;assert(!apply(base,corrupt,result));
   corrupt.resize(100);assert(!apply(base,corrupt,result));
   auto wrong=base;wrong[0]^=1;assert(!apply(wrong,patch,result));
  }count++;
 }
 assert(count==410);std::cout<<"PASS: all 410 patches reconstruct identical translated archives; bounded parser, corrupted patch/base rejection, private cache verified.\n";
}
