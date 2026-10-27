#ifndef CV_SHADOW_SWITCH_H
#define CV_SHADOW_SWITCH_H
/* Exact known texture-free moment-shadow generator. Never suppress a main
 * framebuffer, UI, clear, copy, lighting receiver or unknown shader. */
static int cv_shadow_skip(int enabled,int vp,int clear,int copy,unsigned rt,
    int exp32,unsigned bytes,unsigned code,int rts) {
 return !enabled&&vp&&!clear&&!copy&&rt&&exp32&&bytes==240&&
        code==0x7EA4C4E7u&&rts==1;
}
#endif
