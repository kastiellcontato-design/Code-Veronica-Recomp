#define NOMINMAX
#include <windows.h>
#include "rsx_pose_compare.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <limits>
static uint32_t seed=0xB817AB3;static uint32_t random32(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static volatile uint64_t result;
int main(){
    unsigned cases=0;
    for(unsigned n=0;n<=257;n++)for(unsigned trial=0;trial<512;trial++){
        std::vector<float>a(n*3),b(n*3);
        for(unsigned i=0;i<n*3;i++){
            a[i]=float(int(random32()%10000)-5000)/32;
            b[i]=(trial%4==0)?a[i]:a[i]+float(int(random32()%2000)-1000)/256;
        }
        if(n){unsigned i=random32()%(n*3);switch(trial%16){
        case 0:b[i]=a[i]+8;break;case 1:b[i]=std::nextafter(a[i]+8,INFINITY);break;
        case 2:b[i]=std::nextafter(a[i]+8,-INFINITY);break;case 3:a[i]=0.f;b[i]=-0.f;break;
        case 4:a[i]=b[i]=std::numeric_limits<float>::infinity();break;
        case 5:a[i]=std::numeric_limits<float>::quiet_NaN();break;case 6:a[i]=1e30f;b[i]=-1e30f;break;
        default:break;}}
        int c0=trial&1,c1=c0;int j0=rsx_pose_compare_scalar(a.data(),b.data(),n,&c0),j1=rsx_pose_compare_vectors(a.data(),b.data(),n,&c1);
        assert(j0==j1);if(!j0)assert(c0==c1);cases++;
    }
    // Final three floats end at a guard page. SIMD must not load a fourth.
    auto p=(float*)VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);DWORD prior;assert(p);assert(VirtualProtect((char*)p+4096,4096,PAGE_NOACCESS,&prior));
    for(unsigned n=1;n<=33;n++){
        float* a=(float*)((char*)p+4096)-n*3;for(unsigned i=0;i<n*3;i++)a[i]=float(i);
        std::vector<float>b(a,a+n*3);int changed=0;assert(!rsx_pose_compare_vectors(a,b.data(),n,&changed)&&!changed);
    }VirtualFree(p,0,MEM_RELEASE);
    printf("PASS %u scalar/SSE2 differential cases plus guard-page tails; jump and visible change outputs identical\n",cases);
    std::vector<float>a(4096*3),b(a.size());for(unsigned i=0;i<a.size();i++){a[i]=float(i%97);b[i]=a[i]+.125f;}
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    auto bench=[&](bool vector){uint64_t sum=0;LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
        for(unsigned i=0;i<10000;i++){int changed=0; a[0]=float(i%3);sum+=vector?rsx_pose_compare_vectors(a.data(),b.data(),4096,&changed):rsx_pose_compare_scalar(a.data(),b.data(),4096,&changed);sum+=changed;}
        QueryPerformanceCounter(&t1);result=sum;return (t1.QuadPart-t0.QuadPart)*1e9/f.QuadPart/10000;};
    for(int r=0;r<3;r++)printf("COMPARE MICROBENCH scalar_ns=%.1f vector_ns=%.1f (4096 vertices; not FPS)\n",bench(false),bench(true));
}
