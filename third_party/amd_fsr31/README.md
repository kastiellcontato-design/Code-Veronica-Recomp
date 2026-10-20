# AMD FSR Frame Generation integration dependency

Official GPUOpen FidelityFX-SDK revision: `60f4ea81909200d8542eca14dccb2628b763a9a3`.
Source: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
Runtime: signed `amd_fidelityfx_framegeneration_dx12.dll`, provider 3.1.6;
swapchain API 3.1.7. API headers retain upstream relative layout and notices.
`LICENSE.md` is the unmodified upstream combined redistribution/third-party
notice; include it with distribution of the runtime. Headers have their own
MIT notices. The DLL is AMD-signed; do not modify, disassemble or rename it.

Runtime SHA256: `02297beedd285e822d3a64f314cf00faf378dcec0edc47ff0c4dd71b3a8c2f18`.

The application dynamically loads the DLL only for explicit 60 FPS frame
generation. DLL absence or unsupported hardware falls back to ordinary
presentation. CMake copies the runtime beside the executable. The SDK DLL
must remain resident while the host owns its COM swapchain proxy.
