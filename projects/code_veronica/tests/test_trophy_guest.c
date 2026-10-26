/* Guest-memory and callback fixture for the production sceNpTrophy frontend. */
#include "../../../libs/network/sceNpTrophy.c"
#include <assert.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo,g_ww_hi;
void ppu_resv_break_store(uint64_t ea){(void)ea;}
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w){(void)a;(void)v;(void)w;}
int spu_coh_is_reserved(uint32_t a){(void)a;return 0;}
void spu_coh_notify_write(uint32_t a){(void)a;}
void spu_lockline_lock(void){}
void spu_lockline_unlock(void){}
ps3_guest_caller_fn g_ps3_guest_caller;
static unsigned status_calls;
static void callback(u32 opd,u64 context,u64 status,u64 completed,u64 total,u64 arg,u64 a,u64 b,u64 c){
 assert(opd==0x300&&context==1&&arg==0xCAFE);assert(!completed&&!total&&!a&&!b&&!c);
 assert(status==(status_calls?8:3));status_calls++;
}
static u64 deferred[8];static u32 deferred_opd;
s32 cellSysutilQueueGuestCallbackArgs(u32 opd,const u64 args[8]){memcpy(deferred,args,sizeof deferred);deferred_opd=opd;return CELL_OK;}
void trophy_test_open(const char* id){
 vm_base=calloc(1,65536);assert(vm_base);memcpy(vm_base+0x100,id,9);vm_base[0x10a]=(u8)atoi(id+10);
 assert(sceNpTrophyInit(0,0,0,0)==CELL_OK);
 assert(sceNpTrophyCreateContext((void*)0x200,(void*)0x100,0,0)==CELL_OK);assert(vm_read32(0x200)==1);
 assert(sceNpTrophyCreateHandle((void*)0x204)==CELL_OK);assert(vm_read32(0x204)==1);
 g_ps3_guest_caller=callback;status_calls=0;
 assert(sceNpTrophyRegisterContext(1,1,0x300,0xCAFE,0)==CELL_OK);assert(status_calls==1);
 callback(deferred_opd,deferred[0],deferred[1],deferred[2],deferred[3],deferred[4],deferred[5],deferred[6],deferred[7]);assert(status_calls==2);
 assert(sceNpTrophyGetRequiredDiskSpace(1,1,(void*)0x208,0)==CELL_OK&&vm_read64(0x208)==1048576);
}
int trophy_test_unlock(int id){return sceNpTrophyUnlockTrophy(1,1,id,(void*)0x20c);}
void trophy_test_state(unsigned count,unsigned flags){
 assert(sceNpTrophyGetTrophyUnlockState(1,1,(void*)0x220,(void*)0x230)==CELL_OK);
 assert(vm_read32(0x230)==count&&vm_read32(0x220)==flags&&vm_read32(0x224)==0);
 assert(sceNpTrophyGetGameInfo(1,1,(void*)0x1000,(void*)0x1800)==CELL_OK);
 assert(vm_read32(0x1000)==12&&vm_read32(0x1004)==0&&vm_read32(0x1008)==1&&vm_read32(0x100c)==2&&vm_read32(0x1010)==9);
 assert(sceNpTrophyGetTrophyInfo(1,1,9,(void*)0x2000,(void*)0x2800)==CELL_OK);
 assert(vm_read32(0x2000)==9&&vm_read32(0x2004)==2);assert(strstr((char*)vm_base+0x2008,"Weapon Crazy"));
 assert(sizeof(SceNpTrophyDetails)==1164&&sizeof(SceNpTrophyGameDetails)==1176&&sizeof(SceNpTrophyData)==16);
 if(flags&1){assert(sceNpTrophyGetTrophyInfo(1,1,0,0,(void*)0x2800)==CELL_OK);assert(vm_read64(0x2800)>62135596800000000ULL&&vm_read32(0x2808)==0&&vm_base[0x280c]==1);}
}
void trophy_test_close(void){assert(sceNpTrophyTerm()==CELL_OK);free(vm_base);vm_base=0;}
