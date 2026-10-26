# Bug reports and lightweight support log

The GitHub Bug report form collects port version, OS, GPU/driver, CPU/RAM, region, game location, reproduction steps, graphics/mod settings, prior working version, bin/run-log.txt and screenshot/video links. Required log textarea accepts an attachment or an explanation when no log was created. It deliberately has no code-block render setting, so attachments work. Blank issues remain available. Form files use JSON, which is valid YAML, for standard-library schema checks without an extra dependency.

## Comparison with the supplied SpartanPort log

The supplied PS4 log identifies its title/update, executable hash, applied patches, chosen GPU, unavailable Vulkan extensions/formats, pipeline cache/warmup, execution configuration and a terminal CPU fault with registers and stack offsets. The reported read at address 0x8 suggests a null-pointer access; it does not establish that a GPU extension warning caused that crash. PS4 guest offsets and Vulkan feature names cannot be copied into a PS3/D3D12 port.

This port already logs adapter/VRAM, shader and cache setup, game/region information, FSR status, audio initialization and DRED on GPU-device loss. DRED reports breadcrumbs/page faults around a GPU failure; a normal run-log is not a complete performance trace.

## Added in source for the next binary build

- Once after opening the shared log: Windows kernel/build, CPU model/vendor, logical CPUs/groups, SSE4.1, AVX/AVX2 hardware and OS XSTATE readiness, RAM total/available, compiled region and process ID. No machine identifier, username, serial number or full environment dump is added.
- Once after game configuration: an allowlist of effective runtime environment settings. Unset values explicitly say default, not a guessed value. Film-filter state and other renderer setup remain in their existing logs.
- Once after selecting the actual D3D12 device: adapter IDs, driver version if available, feature level, shader-model query (capped at6.6), wave capabilities and four formats relevant to frame generation/depth. Unavailable queries are marked unavailable and do not disable features.
- On an unhandled terminal Windows SEH exception: exception code, thread, fault address/read-write-execute when applicable, x64 registers and up to24 host module+offset frames. It uses the faulting CONTEXT and does not install a first-chance handler, alter guest page commitment or recover execution. A previous terminal filter is retained. Stack collection is best effort; abort, forced termination, hangs and some driver failures may not reach this filter. No memory dump is saved. Host offsets need matching build/PDBs and are not PS3 guest offsets.

These changes do not create periodic sampling, background collectors, F12 capture or per-draw logging. They add startup work and best-effort failure reporting; no zero-cost or complete diagnostic coverage claim is made.

Validation: CPU/OS startup and AVX OS-state checks, synthetic terminal illegal-instruction crash with stack, query-only AMD hardware and WARP capability reports, and compilation of production main/GUI entry/D3D12 backend. Artifacts remain in teste-relatorio-suporte-v1.1.4. The public v1.1.4 ZIP was not replaced. The form becomes available after pushing to the repository default branch; new log fields require a future executable build.
