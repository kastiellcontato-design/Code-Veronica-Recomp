#ifndef RSX_SUPPORT_CAPS_H
#define RSX_SUPPORT_CAPS_H
/* Include after D3D12/DXGI COM headers. Query-only startup facts, no renderer policy changes. */
static void rsx_support_caps(ID3D12Device* device,IDXGIFactory4* factory){
 IDXGIAdapter1* adapter=NULL;LUID luid;device->lpVtbl->GetAdapterLuid(device,&luid);
 if(SUCCEEDED(factory->lpVtbl->EnumAdapterByLuid(factory,luid,&IID_IDXGIAdapter1,(void**)&adapter))&&adapter){
  DXGI_ADAPTER_DESC1 d;memset(&d,0,sizeof d);
  if(SUCCEEDED(adapter->lpVtbl->GetDesc1(adapter,&d)))fprintf(stderr,"[SUPPORT-GPU] selected=%ls vendor=0x%04X device=0x%04X revision=%u dedicated_MB=%llu shared_MB=%llu software=%u\n",d.Description,d.VendorId,d.DeviceId,d.Revision,(unsigned long long)(d.DedicatedVideoMemory>>20),(unsigned long long)(d.SharedSystemMemory>>20),!!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE));
  LARGE_INTEGER driver;HRESULT hr=adapter->lpVtbl->CheckInterfaceSupport(adapter,&IID_IDXGIDevice,&driver);
  if(SUCCEEDED(hr))fprintf(stderr,"[SUPPORT-GPU] DXGI_driver=%u.%u.%u.%u\n",(unsigned)((uint64_t)driver.QuadPart>>48)&65535,(unsigned)((uint64_t)driver.QuadPart>>32)&65535,(unsigned)((uint64_t)driver.QuadPart>>16)&65535,(unsigned)driver.QuadPart&65535);
  else fprintf(stderr,"[SUPPORT-GPU] driver_version=unavailable hr=0x%08lX (copy vendor driver version into issue form)\n",(unsigned long)hr);
  adapter->lpVtbl->Release(adapter);
 }
 D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
 D3D12_FEATURE_DATA_FEATURE_LEVELS levels={5,requested,D3D_FEATURE_LEVEL_11_0};
 if(SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device,D3D12_FEATURE_FEATURE_LEVELS,&levels,sizeof levels)))fprintf(stderr,"[SUPPORT-GPU] max_feature_level=0x%X\n",levels.MaxSupportedFeatureLevel);
 D3D12_FEATURE_DATA_SHADER_MODEL sm={D3D_SHADER_MODEL_6_6};HRESULT hr=device->lpVtbl->CheckFeatureSupport(device,D3D12_FEATURE_SHADER_MODEL,&sm,sizeof sm);
 if(SUCCEEDED(hr))fprintf(stderr,"[SUPPORT-GPU] shader_model_query=0x%X (query capped at 6.6)\n",sm.HighestShaderModel);
 else fprintf(stderr,"[SUPPORT-GPU] shader_model_query=unavailable hr=0x%08lX\n",(unsigned long)hr);
 D3D12_FEATURE_DATA_D3D12_OPTIONS1 options;memset(&options,0,sizeof options);
 if(SUCCEEDED(device->lpVtbl->CheckFeatureSupport(device,D3D12_FEATURE_D3D12_OPTIONS1,&options,sizeof options)))fprintf(stderr,"[SUPPORT-GPU] wave_ops=%u wave_lanes_min=%u max=%u\n",options.WaveOps,options.WaveLaneCountMin,options.WaveLaneCountMax);
 const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM};
 for(unsigned i=0;i<4;i++){D3D12_FEATURE_DATA_FORMAT_SUPPORT f={formats[i],0,0};HRESULT result=device->lpVtbl->CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&f,sizeof f);fprintf(stderr,"[SUPPORT-GPU-FORMAT] format=%u support1=0x%X support2=0x%X hr=0x%08lX\n",formats[i],f.Support1,f.Support2,(unsigned long)result);}
}
#endif
