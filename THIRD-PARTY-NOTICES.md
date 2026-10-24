# Third-party notices

The Resident Evil Code: Veronica X PC port and the ps3recomp toolkit are released under the MIT License ([LICENSE](LICENSE)). They include or ship the third-party components below, each under its own license. The full license texts are in the files listed; the release package carries them in its `licenses` folder.

**The game is not covered by any of these licenses.** *Resident Evil Code: Veronica X* (© CAPCOM CO., LTD.) is not included and must be supplied by the user from their own copy.

| Component | Used for | License | Full text |
|---|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) © Ned Heller / sp00nznet and contributors | Recompiler, runtime and PS3 library reimplementations | MIT | [LICENSE](LICENSE) |
| [FFmpeg](https://ffmpeg.org) 7.1 (`avcodec-61`, `avutil-59`, `swscale-8`, `swresample-5`, shared DLLs) | MPEG-2 movie decoding | LGPL 2.1 or later | [third_party/ffmpeg/COPYING.LGPLv2.1](third_party/ffmpeg/COPYING.LGPLv2.1), [third_party/ffmpeg/LICENSE.md](third_party/ffmpeg/LICENSE.md) |
| `at3_standalone`: FFmpeg's ATRAC3/ATRAC3plus decoders as extracted by [PPSSPP](https://github.com/hrydgard/ppsspp) | Game audio (ATRAC3plus) | LGPL 2.1 or later | [third_party/at3_standalone/COPYING.LGPLv2.1](third_party/at3_standalone/COPYING.LGPLv2.1), [README](third_party/at3_standalone/README.txt) |
| AMD FidelityFX FSR 3.1 frame generation runtime (`amd_fidelityfx_framegeneration_dx12.dll`) © Advanced Micro Devices, Inc. | Optional frame generation | AMD FidelityFX SDK license (binary redistribution) | [third_party/amd_fsr31/LICENSE.md](third_party/amd_fsr31/LICENSE.md) (shipped as `AMD-FSR-LICENSE.md`) |
| AMD FidelityFX Super Resolution 1.0 (EASU, RCAS), port of `ffx_fsr1.h` © 2021 Advanced Micro Devices, Inc. | Sharpening and upscaling | MIT | [below](#amd-fidelityfx-super-resolution-10) and in `libs/video/rsx_d3d12_backend.c` |
| [SMAA](https://www.iryoku.com/smaa/) © 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez | Anti-aliasing | MIT | [third_party/smaa/LICENSE.txt](third_party/smaa/LICENSE.txt) |
| FXAA 3.11 by Timothy Lottes (NVIDIA) | Anti-aliasing | NVIDIA FXAA 3.11 permissive license | Port in `libs/video/rsx_d3d12_backend.c` |
| [stb_image, stb_image_write](https://github.com/nothings/stb) by Sean Barrett | Image decoding and PNG writing | Public domain or MIT (choice) | Headers in `libs/codec/` |
| [GitHub Octicons](https://github.com/primer/octicons) © GitHub Inc. | GitHub mark in the launcher | MIT | [projects/code_veronica/launcher_art/github-octicons-LICENSE.txt](projects/code_veronica/launcher_art/github-octicons-LICENSE.txt) |
| [recvx-enhanced-port](https://github.com/aikatore/recvx-enhanced-port) © 2026 aikatore and contributors | Room location labels (`cv_save_rooms.h`) | MIT | [projects/code_veronica/SAVE-LIBRARY-NOTICES.txt](projects/code_veronica/SAVE-LIBRARY-NOTICES.txt) |
| [re-cvx-autosplitter](https://github.com/kapdap/re-cvx-autosplitter) © 2025 Kapdap | LiveSplit script (`livesplit/recvx-ps3recomp.asl`) | MIT | [projects/code_veronica/livesplit/LICENSE-recvx.txt](projects/code_veronica/livesplit/LICENSE-recvx.txt) |

## References (no code included)

- [RPCS3](https://rpcs3.net) (GPL-2.0): used as the behaviour reference for the PS3 hardware and system, for frame captures and comparisons, and for its `patch.yml` patch format. The door-skip patch comes from the RPCS3 patch list (by Kapdap).
- [re-cvx-srt-provider](https://github.com/kapdap/re-cvx-srt-provider) by Kapdap (MIT): its memory addresses and game-state rules were studied; the SRT reader and layout are an independent implementation.

## AMD FidelityFX Super Resolution 1.0

```
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## FFmpeg and LGPL

The FFmpeg libraries are used as separate, replaceable DLLs next to the executables. Their source code is available at <https://github.com/FFmpeg/FFmpeg/tree/n7.1.5>. The `at3_standalone` decoder sources are included unmodified (apart from small build shims) in [third_party/at3_standalone](third_party/at3_standalone), together with the rest of this repository's source code, so the program can be rebuilt with a modified version of them.

## zlib

zlib 1.3.2, by Jean-loup Gailly and Mark Adler, is statically linked for local
appearance archive preparation. Unmodified upstream sources and pinned download
checksum: `third_party/zlib/UPSTREAM.json`. License: `licenses/zlib-LICENSE.txt`
in the player package and `third_party/zlib/LICENSE` in the source tree.
