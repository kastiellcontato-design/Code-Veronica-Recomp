#include "../cv_support_log.h"
#include <cassert>
int main(int argc,char**){
 assert(!cv_support::avx_os_ready(0,6));assert(!cv_support::avx_os_ready((1u<<27)|(1u<<28),2));assert(cv_support::avx_os_ready((1u<<27)|(1u<<28),6));
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 cv_support::startup("SYNTHETIC");cv_support::settings();
 if(argc>1)RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION,0,0,nullptr);
 puts("PASS startup CPU/OS report and AVX OS-state checks");
}
