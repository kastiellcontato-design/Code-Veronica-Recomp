#pragma once
#include <cstring>
#include <cstdio>
#include <cstdlib>
inline bool cv_ptbr_choices_install() {
    const char* root=std::getenv("CV_PTBR_ASSET_ROOT");if(!root||!*root)return true;
    const bool is_us=!std::strcmp(CV_TITLE_ID,"NPUB30467");
    if(std::strcmp(CV_TITLE_ID,"NPEB00553")&&!is_us)return false;
    // The native UI uses 48-byte records: count, ten glyphs, terminator.
    // These English Yes/No/OK records are not part of sysmes.msb.
    const unsigned addresses[]={is_us?0x87A950u:0x87D300u,is_us?0x87A980u:0x87D330u,is_us?0x87A9B0u:0x87D360u};
    const unsigned expected[3][12]={{3,57,69,83,0xffffffffu},{2,46,79,0xffffffffu},{2,47,43,0xffffffffu}};
    const unsigned replacement[3][12]={
        {3,51,73,77,0xffffffffu}, // Sim
        {4,46,160,104,79,0xffffffffu}, // Não + native invisible a advance
        {9,35,79,78,70,73,82,77,65,82,0xffffffffu} // Confirmar
    };
    if(!vm_base||(ppu_vm_size&&ppu_vm_size<addresses[2]+48))return false;
    for(unsigned r=0;r<3;r++)for(unsigned i=0;i<12;i++) {
        const auto p=vm_base+addresses[r]+i*4;
        const unsigned value=(unsigned(p[0])<<24)|(unsigned(p[1])<<16)|(unsigned(p[2])<<8)|p[3];
        if(value!=expected[r][i]){std::fprintf(stderr,"[PTBR-CHOICES] incompatible native record; not applied\n");return false;}
    }
    for(unsigned r=0;r<3;r++)for(unsigned i=0;i<12;i++) {
        auto p=vm_base+addresses[r]+i*4;unsigned value=replacement[r][i];
        p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;
    }
    std::fprintf(stderr,"[PTBR-CHOICES] private Sim/Nao/Confirmar records installed\n");return true;
}
// Applied only in the EU/US local localization runners, after loading its exact
// original ELF. Six invisible glyph slots acquire the base letters' widths.
// No code instructions, existing text glyphs, files or controller icons change.
inline bool cv_ptbr_spacing_install() {
    const char* root=std::getenv("CV_PTBR_ASSET_ROOT");
    if(!root||!*root)return true;
    const bool is_us=!std::strcmp(CV_TITLE_ID,"NPUB30467");
    if(std::strcmp(CV_TITLE_ID,"NPEB00553")&&!is_us){
        std::fprintf(stderr,"[PTBR-SPACING] unsupported title: %s\n",CV_TITLE_ID);return false;
    }
    const unsigned proportional=is_us?0x8780BCu:0x87A308u, alternate=0x87A386;
    // The Windows runner sets ppu_vm_size=0 for its full 32-bit guest mapping;
    // zero is the runtime's explicit "no range guard needed" sentinel.
    if(!vm_base||(ppu_vm_size!=0&&ppu_vm_size<(is_us?proportional+126:alternate+162))){
        std::fprintf(stderr,"[PTBR-SPACING] invalid guest mapping; bounded size=%u\n",ppu_vm_size);return false;
    }
    // Slot zero is blank in this atlas and is not the FF01 word-space
    // command. All translated word spaces remain FF01. One slot gives
    // capitals exactly 24/16, instead of two c-slots giving 24/28.
    const unsigned from[]={65,69,73,85,67,33};
    const unsigned to[]={104,105,106,107,64,0};
    const unsigned expected_p[]={16,14,10,18,12,24};
    const unsigned expected_a[]={14,14,12,14,14,16};
    const unsigned original_p[]={28,28,28,28,28,28};
    const unsigned original_a[]={14,14,14,14,28,28};
    // Check everything before the first write: fail closed on another binary,
    // a modified width table, or an incompatible existing patch.
    for(unsigned i=0;i<6;i++) {
        if(vm_base[proportional+from[i]]!=expected_p[i]||
           (!is_us&&vm_base[alternate+from[i]]!=expected_a[i])||
           vm_base[proportional+to[i]]!=original_p[i]||
           (!is_us&&vm_base[alternate+to[i]]!=original_a[i])) {
            std::fprintf(stderr,"[PTBR-SPACING] incompatible native metrics at slot %u; not applied\n",to[i]);
            return false;
        }
    }
    for(unsigned i=0;i<6;i++) {
        vm_base[proportional+to[i]]=vm_base[proportional+from[i]];
        if(!is_us)vm_base[alternate+to[i]]=vm_base[alternate+from[i]];
    }
    std::fprintf(stderr,"[PTBR-SPACING] base-letter advances installed once; original word spaces preserved\n");
    return true;
}
