/* Process-local 30 FPS comparison policy. Unset/invalid preserves existing
 * behavior; explicit 0/1 selects the native-clock comparison package. */
#ifndef RSX_RENDER_OPTIMIZATIONS_H
#define RSX_RENDER_OPTIMIZATIONS_H
static inline int rsx_opt30_parse(const char* value) {
    return value&&value[0]&&value[1]==0&&(value[0]=='0'||value[0]=='1')?value[0]-'0':-1;
}
static inline int rsx_opt30_mode(const char* value,const char* fps60) {
    return fps60&&fps60[0]=='1'?-1:rsx_opt30_parse(value);
}
static inline int rsx_opt30_prep(int mode,int pose,int disabled) {
    return !disabled&&(pose||mode==1);
}
static inline int rsx_opt30_shadow(int mode,int legacy60,int disabled) {
    return !disabled&&(mode==1||(mode<0&&legacy60));
}
#endif
