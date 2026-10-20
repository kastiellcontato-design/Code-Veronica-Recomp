#ifndef RSX_FSR31_H
#define RSX_FSR31_H
#ifdef __cplusplus
extern "C" {
#endif
/* All calls belong to the render thread. Disabled mode allocates nothing.
 * begin is called only after the host graphics fence has completed. */
int rsx_fsr31_requested(void);
int rsx_fsr31_swapchain(void* device,void* queue,void* factory,void* hwnd,void* desc,void** output);
void rsx_fsr31_wait(void);
void rsx_fsr31_capture_next(void);
int rsx_fsr31_begin(void* device,unsigned width,unsigned height,unsigned scene,int reset);
void rsx_fsr31_depth(void* command_list,void* depth,unsigned width,unsigned height);
void rsx_fsr31_hudless(void* command_list,void* color,unsigned state);
void rsx_fsr31_present_color(void* command_list,void* color,unsigned state);
void rsx_fsr31_frame(void* command_list,void* swapchain,int eligible);
void rsx_fsr31_shutdown(void);
/* Diagnostics distinguish real renders from requested generated workloads. */
unsigned long long rsx_fsr31_generated_jobs(void);
/* Render-thread query: real DXGI presentation count, including generated
 * frames. 1=valid, 0=FSR off/failed, -1=counter unavailable. Unsigned deltas
 * handle DXGI's 32-bit wrap. Present submission is not monitor scanout. */
int rsx_fsr31_present_count(unsigned* count);
#ifdef __cplusplus
}
#endif
#endif
