#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t u32;
#define CELL_AUDIO_SAMPLE_RATE 48000
static unsigned long long g_audio_dropped;
const IID IID_IAudioClient={0x1CB9AD4C,0xDBFA,0x4c32,{0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
const IID IID_IAudioRenderClient={0xF294ACFC,0x3146,0x4483,{0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2}};
static HRESULT WINAPI mock_coinit(LPVOID p,DWORD flags);
static HRESULT WINAPI mock_cocreate(REFCLSID cls,LPUNKNOWN outer,DWORD flags,REFIID iid,LPVOID* out);
#define CoInitializeEx mock_coinit
#define CoCreateInstance mock_cocreate
#include "wasapi-under-test.h"
#undef CoInitializeEx
#undef CoCreateInstance

typedef struct { IAudioClient iface;int initialized,released;DWORD flags; } Client;
static Client clients[8];static int scenario,created,attempts,started,render_releases,device_releases,event_calls;
static IMMDeviceEnumerator enumerator;static IMMDevice device;static IAudioRenderClient render;
static ULONG STDMETHODCALLTYPE enum_release(IMMDeviceEnumerator* p){return 0;}
static HRESULT STDMETHODCALLTYPE endpoint(IMMDeviceEnumerator* p,EDataFlow flow,ERole role,IMMDevice** out){assert(flow==eRender&&role==eConsole);*out=&device;return S_OK;}
static ULONG STDMETHODCALLTYPE device_release(IMMDevice* p){device_releases++;return 0;}
static HRESULT STDMETHODCALLTYPE activate(IMMDevice* p,REFIID iid,DWORD flags,PROPVARIANT* pv,void** out){assert(created<8);Client* c=&clients[created++];*out=&c->iface;return S_OK;}
static ULONG STDMETHODCALLTYPE client_release(IAudioClient* p){Client* c=(Client*)p;assert(!c->released);c->released=1;return 0;}
static HRESULT STDMETHODCALLTYPE initialize(IAudioClient* p,AUDCLNT_SHAREMODE mode,DWORD flags,REFERENCE_TIME duration,REFERENCE_TIME period,const WAVEFORMATEX* f,LPCGUID id){
 Client* c=(Client*)p;assert(!c->initialized&&!c->released);c->initialized=1;c->flags=flags;attempts++;
 assert(mode==AUDCLNT_SHAREMODE_SHARED&&duration==1000000&&period==0);
 assert(f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT&&f->nChannels==2&&f->nSamplesPerSec==48000&&f->wBitsPerSample==32&&f->nBlockAlign==8&&f->nAvgBytesPerSec==384000);
 int convert=(flags&AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM)!=0,event=(flags&AUDCLNT_STREAMFLAGS_EVENTCALLBACK)!=0;
 if(convert)assert(flags&AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
 if(scenario==4)return AUDCLNT_E_UNSUPPORTED_FORMAT;
 if(scenario==8)return AUDCLNT_E_DEVICE_INVALIDATED;
 if((scenario==1||scenario==2||scenario==3)&&!convert)return AUDCLNT_E_UNSUPPORTED_FORMAT;
 if((scenario==3||scenario==7)&&event)return E_INVALIDARG;
 return S_OK;
}
static HRESULT STDMETHODCALLTYPE get_mix(IAudioClient* p,WAVEFORMATEX** out){
 *out=CoTaskMemAlloc(sizeof(WAVEFORMATEX));memset(*out,0,sizeof(WAVEFORMATEX));
 (*out)->nSamplesPerSec=scenario==1?44100:48000;(*out)->nChannels=scenario==2?6:2;(*out)->wBitsPerSample=scenario==1?16:32;return S_OK;
}
static HRESULT STDMETHODCALLTYPE set_event(IAudioClient* p,HANDLE event){assert(event!=NULL);event_calls++;return S_OK;}
static HRESULT STDMETHODCALLTYPE get_size(IAudioClient* p,UINT32* out){*out=4800;return S_OK;}
static HRESULT STDMETHODCALLTYPE get_service(IAudioClient* p,REFIID iid,void** out){if(scenario==5)return E_FAIL;*out=&render;return S_OK;}
static HRESULT STDMETHODCALLTYPE start(IAudioClient* p){started++;return scenario==6?E_FAIL:S_OK;}
static HRESULT STDMETHODCALLTYPE stop(IAudioClient* p){return S_OK;}
static ULONG STDMETHODCALLTYPE render_release(IAudioRenderClient* p){render_releases++;return 0;}
static IMMDeviceEnumeratorVtbl ev={.Release=enum_release,.GetDefaultAudioEndpoint=endpoint};
static IMMDeviceVtbl dv={.Release=device_release,.Activate=activate};
static IAudioClientVtbl cv={.Release=client_release,.Initialize=initialize,.GetBufferSize=get_size,.GetMixFormat=get_mix,.Start=start,.Stop=stop,.SetEventHandle=set_event,.GetService=get_service};
static IAudioRenderClientVtbl rv={.Release=render_release};
static HRESULT WINAPI mock_coinit(LPVOID p,DWORD flags){return S_OK;}
static HRESULT WINAPI mock_cocreate(REFCLSID cls,LPUNKNOWN outer,DWORD flags,REFIID iid,LPVOID* out){*out=&enumerator;return S_OK;}
int main(void){
 enumerator.lpVtbl=&ev;device.lpVtbl=&dv;render.lpVtbl=&rv;
 for(scenario=0;scenario<=8;scenario++){
  memset(clients,0,sizeof clients);for(int i=0;i<8;i++)clients[i].iface.lpVtbl=&cv;
  created=attempts=started=render_releases=device_releases=event_calls=0;
  int result=audio_backend_init();int failure=scenario==4||scenario==5||scenario==6||scenario==8;
  assert((result<0)==failure);assert(device_releases==1);
  if(!failure){
   assert(started==1&&s_wasapi_client&&s_wasapi_render);
   if(scenario==0)assert(created==1&&!(clients[0].flags&AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM));
   if(scenario==1||scenario==2)assert(created==3&&(clients[2].flags&AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM));
   if(scenario==3)assert(created==4&&!s_wasapi_event&&(clients[3].flags&AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM));
   if(scenario==7)assert(created==2&&!s_wasapi_event&&!(clients[1].flags&AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM));
   audio_backend_shutdown();
  }
  assert(!s_wasapi_client&&!s_wasapi_render&&!s_wasapi_event);
  for(int i=0;i<created;i++)assert(clients[i].released);
  printf("PASS WASAPI scenario %d: clients=%d, starts=%d, failure=%d\n",scenario,created,started,failure);
 }
 puts("PASS: native path unchanged; unsupported 44.1k/PCM and multichannel formats convert; polling fallback; fresh clients; complete cleanup on errors.");return 0;
}
