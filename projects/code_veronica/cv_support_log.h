#pragma once
// Startup and terminal-failure facts only: no timers, sampling or background worker.
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <io.h>
namespace cv_support {
inline void raw(const char* format,...){
 char text[2048];va_list args;va_start(args,format);int n=vsnprintf(text,sizeof text,format,args);va_end(args);
 if(n<=0)return;DWORD written;HANDLE h=(HANDLE)_get_osfhandle(_fileno(stderr));
 if(h!=INVALID_HANDLE_VALUE)WriteFile(h,text,(DWORD)(n<(int)sizeof text?n:sizeof text-1),&written,nullptr);
}
inline void address(unsigned index,DWORD64 pc){
 HMODULE m=nullptr;char path[MAX_PATH]={};
 if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)pc,&m)){
  GetModuleFileNameA(m,path,sizeof path);const char* base=strrchr(path,'\\');base=base?base+1:path;
  raw("[SUPPORT-CRASH] #%u %s+0x%llX\n",index,base,(unsigned long long)(pc-(DWORD64)m));
 }else raw("[SUPPORT-CRASH] #%u host=0x%llX\n",index,(unsigned long long)pc);
}
inline LPTOP_LEVEL_EXCEPTION_FILTER previous_filter=nullptr;
inline volatile LONG reporting=0;
inline LONG WINAPI terminal(EXCEPTION_POINTERS* ep){
 if(!ep||InterlockedCompareExchange(&reporting,1,0))return EXCEPTION_CONTINUE_SEARCH;
 const auto* e=ep->ExceptionRecord;CONTEXT c=*ep->ContextRecord;
 raw("[SUPPORT-CRASH] unhandled code=0x%08lX tid=%lu rip=0x%llX (host offsets; match the build/PDB)\n",e->ExceptionCode,GetCurrentThreadId(),(unsigned long long)c.Rip);
 if((e->ExceptionCode==EXCEPTION_ACCESS_VIOLATION||e->ExceptionCode==EXCEPTION_IN_PAGE_ERROR)&&e->NumberParameters>=2)
  raw("[SUPPORT-CRASH] memory_operation=%llu address=0x%llX (0=read,1=write,8=execute)\n",(unsigned long long)e->ExceptionInformation[0],(unsigned long long)e->ExceptionInformation[1]);
 if(e->ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION)raw("[SUPPORT-CRASH] illegal instruction: compare CPU/OS AVX2 support above; this code alone does not prove a CPU mismatch\n");
 raw("[SUPPORT-CRASH] rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX rbp=%llX rsp=%llX\n",c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi,c.Rbp,c.Rsp);
 raw("[SUPPORT-CRASH] r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX r13=%llX r14=%llX r15=%llX\n",c.R8,c.R9,c.R10,c.R11,c.R12,c.R13,c.R14,c.R15);
 __try{
  for(unsigned i=0;i<24&&c.Rip;i++){
   address(i,c.Rip);DWORD64 base=0;auto fn=RtlLookupFunctionEntry(c.Rip,&base,nullptr);DWORD64 old_sp=c.Rsp;
   if(fn){void* data=nullptr;DWORD64 establisher=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,fn,&c,&data,&establisher,nullptr);}
   else{c.Rip=*(DWORD64*)c.Rsp;c.Rsp+=8;}
   if(c.Rsp<=old_sp)break;
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){raw("[SUPPORT-CRASH] stack walk unavailable or truncated\n");}
 raw("[SUPPORT-CRASH] end; no game-memory dump was written\n");
 return previous_filter?previous_filter(ep):EXCEPTION_CONTINUE_SEARCH;
}
inline bool avx_os_ready(unsigned ecx,unsigned long long xcr){return (ecx&(1u<<27))&&(ecx&(1u<<28))&&((xcr&6)==6);}
inline void startup(const char* region){
 int r[4]={};__cpuid(r,0);unsigned max=(unsigned)r[0];char vendor[13]={};memcpy(vendor,r+1,4);memcpy(vendor+4,r+3,4);memcpy(vendor+8,r+2,4);
 unsigned ecx=0,ebx7=0;unsigned long long xcr=0;
 if(max>=1){__cpuid(r,1);ecx=(unsigned)r[2];if(ecx&(1u<<27))xcr=_xgetbv(0);}
 if(max>=7){__cpuidex(r,7,0);ebx7=(unsigned)r[1];}
 char brand[49]={};__cpuid(r,0x80000000);if((unsigned)r[0]>=0x80000004)for(int i=0;i<3;i++){__cpuid(r,0x80000002+i);memcpy(brand+i*16,r,16);}
 MEMORYSTATUSEX memory={sizeof memory};BOOL have_memory=GlobalMemoryStatusEx(&memory);
 OSVERSIONINFOEXW os={sizeof os};using VersionFn=LONG(WINAPI*)(OSVERSIONINFOEXW*);auto version=(VersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
 fprintf(stderr,"[SUPPORT] schema=1 region=%s process=x64 pid=%lu\n",region,GetCurrentProcessId());
 if(version&&version(&os)==0)fprintf(stderr,"[SUPPORT] Windows kernel=%lu.%lu build=%lu product_type=%u\n",os.dwMajorVersion,os.dwMinorVersion,os.dwBuildNumber,os.wProductType);
 fprintf(stderr,"[SUPPORT] CPU=%s vendor=%s logical_processors=%lu groups=%u SSE4.1=%u AVX_hw=%u AVX2_hw=%u AVX_OS_ready=%u AVX2_usable=%u\n",brand,vendor,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),GetActiveProcessorGroupCount(),!!(ecx&(1u<<19)),!!(ecx&(1u<<28)),!!(ebx7&(1u<<5)),avx_os_ready(ecx,xcr),!!(ebx7&(1u<<5))&&avx_os_ready(ecx,xcr));
 if(have_memory)fprintf(stderr,"[SUPPORT] RAM total_MB=%llu available_MB=%llu\n",memory.ullTotalPhys>>20,memory.ullAvailPhys>>20);
 previous_filter=SetUnhandledExceptionFilter(terminal);
}
inline void settings(){
 static const char* keys[]={"RES_SCALE","VBLANK_HZ","CV_FPS_TEST_60","CV_RENDER_INTERPOLATION","CV_POSE_COMPUTE","CV_FSR_FRAME_GENERATION","CV_FSR_ASYNC","CV_SHARPNESS","AA_MODE","RSX_ANISO","VSYNC","RSX_GPU_VFETCH","SPU_MAX_RUNNING","SPU_TASK_POOL","CV_MUSIC_VOLUME","CV_SFX_VOLUME"};
 for(const char* key:keys){const char* value=getenv(key);fprintf(stderr,"[SUPPORT-SETTING] %s=%s\n",key,value&&*value?value:"<default>");}
 fprintf(stderr,"[SUPPORT] startup facts only; no performance sampling enabled by this header\n");
}
}
#endif
