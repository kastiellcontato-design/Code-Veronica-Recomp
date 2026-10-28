#pragma once
#include <string.h>
#include "pad_map.h"
/* Binding IDs extend the UI only: PadPhys and its ABI stay unchanged. */
enum { CV_AXIS_FIRST=PB_COUNT, CV_BINDING_COUNT=PB_COUNT+8 };
static const char* const cv_axis_ids[8]={"ls_up","ls_down","ls_left","ls_right","rs_up","rs_down","rs_left","rs_right"};
static const char* cv_binding_id(int b){return b>=CV_AXIS_FIRST&&b<CV_BINDING_COUNT?cv_axis_ids[b-CV_AXIS_FIRST]:pad_phys_id(b);}
static int cv_binding_from_id(const char* s){for(int i=0;i<8;i++)if(!strcmp(s,cv_axis_ids[i]))return CV_AXIS_FIRST+i;return pad_phys_from_id(s);}
static int cv_binding_value(const PadPhys* p,int b){
 if(!p->connected||b<0||b>=CV_BINDING_COUNT)return 0;
 if(b<CV_AXIS_FIRST)return (p->buttons&(1u<<b))?128:0;
 int n=b-CV_AXIS_FIRST,d=n%4,v=n<4?((d<2)?p->ly:p->lx):((d<2)?p->ry:p->rx);
 v=(d==0||d==2)?128-v:v-128;return v>0?v:0;
}
static unsigned cv_binding_mask(const PadPhys* p){unsigned mask=p->connected?p->buttons:0;for(int i=CV_AXIS_FIRST;i<CV_BINDING_COUNT;i++)if(cv_binding_value(p,i)>70)mask|=1u<<i;return mask;}
static unsigned char cv_axis_pair(int negative,int positive){int n=128+positive-negative;return (unsigned char)(n<0?0:n>255?255:n);}
static void cv_map_sticks(const PadPhys* p,const int map[PA_COUNT][PAD_BTN_SLOTS],unsigned char out[4]){
 int v[8]={0};for(int d=0;d<8;d++)for(int k=0;k<PAD_BTN_SLOTS;k++){int n=cv_binding_value(p,map[PA_LS_UP+d][k]);if(n>v[d])v[d]=n;}
 out[0]=cv_axis_pair(v[2],v[3]);out[1]=cv_axis_pair(v[0],v[1]);out[2]=cv_axis_pair(v[6],v[7]);out[3]=cv_axis_pair(v[4],v[5]);
}
