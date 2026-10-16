# Resident Evil Code: Veronica X HD — PS3 static recompilation (hobby port)

A work-in-progress native Windows port of **Resident Evil Code: Veronica X HD**
(PSN, `NPEB00553`), built with [ps3recomp](https://github.com/sp00nznet/ps3recomp).

> **This repository contains no game data and no code from the game.**
> You must own the game and supply your own decrypted copy. Everything lifted
> from the game's executable (`src/recomp/`, `projects/code_veronica/spu/`) is
> generated locally on your machine and is never committed.

## Status

| Area | State |
|---|---|
| Boot | Reaches the title menu |
| FMV (PAMF: MPEG-2 1280×640 @ 29.97 + ATRAC3plus) | Plays through FFmpeg; still slower than real time |
| Gameplay | Not yet |

Experimental. Expect graphical glitches and crashes.

## Requirements

- Windows 10/11 x64, a Direct3D 12 GPU
- Visual Studio 2022+ (or Build Tools) with the C++ workload, **LLVM/clang-cl**, CMake ≥ 3.20, Ninja
- Python 3.12
- Your own copy of the game, decrypted (`USRDIR/EBOOT.elf` plus the game folder)
- The PS3 firmware `dev_flash` (e.g. from an RPCS3 install) and the game's `.rap` license
- FFmpeg 7.1 shared DLLs (`avcodec-61`, `avutil-59`, `swscale-8`, `swresample-5`)
  placed in `third_party/ffmpeg/bin/`

## Building

1. **Put the game** in `JOGO/NPEB00553/` (ignored by git), so that
   `JOGO/NPEB00553/USRDIR/EBOOT.elf` exists.
2. **Lift the game** (generates `src/recomp/` and the SPU sources) by following
   [docs/GETTING_STARTED.md](../../docs/GETTING_STARTED.md).
3. **Build optimized**: run `build-release.cmd`.
   The output is `build-release/bin/MyGameRecomp.exe` with the FFmpeg DLLs copied next to it.

   Use the Release build. With the Debug build the lifted game code runs
   unoptimized (~98% of frame time), and the movies stutter badly.

### Japanese version (NPJB00135, BIOHAZARD CODE: Veronica Kanzenban)

The Japanese PSN release is supported too, as a second executable,
`MyGameRecomp_JP.exe`, beside the European one. Its EBOOT has a free licence,
so no `.rap` is needed.

```
python tools/ppu_loader.py <NPJB00135>/USRDIR/EBOOT.elf -o out_jp/
python tools/ppu_lifter.py <NPJB00135>/USRDIR/EBOOT.elf --functions out_jp/EBOOT.functions.json --hle-stubs out_jp/EBOOT.imports.json --patches projects/code_veronica/patches.json -o src/recomp_jp
set CV_BUILD_JP=1
projects/code_veronica/build-release.cmd
```

The SPU images are the same as the European ones. The port's own guest
addresses for each version are in `cv_version.h`. Either launcher accepts
either version's folder and starts the matching executable.

## Running

Edit the paths at the top of `executar-code-veronica-otimizado.cmd`
(`PS3_VFS_ROOT`, `PS3_DEV_FLASH`, `PS3_RAP_DIR`), then double-click it.
The log is written to `run-log-release.txt`.

Keyboard (without an XInput pad): arrows = D-pad, X = Cross, Z = Circle,
A = Square, S = Triangle, I = Start, R = Select.

## Useful environment variables

| Variable | Effect |
|---|---|
| `PS3_VERBOSE=1` | Per-frame/per-call logging (slows the game down) |
| `CV_VIDEO_PACE_TRACE=1` | Logs movie frame delivery vs. wall clock once a second |
| `CV_AV_TRACE=1` | Traces every decoded picture/PCM frame |

## Credits

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) by sp00nznet and contributors (MIT)
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1) for MPEG-2 and ATRAC3plus decoding
- Resident Evil Code: Veronica © Capcom. This project is not affiliated with Capcom or Sony.

---

## Resumo em português

Port nativo para PC (por hobby) do **Resident Evil Code: Veronica X HD** de PS3,
feito com o ps3recomp. **Não tem nenhum arquivo do jogo aqui.** Você precisa ter
o jogo e gerar o código recompilado na sua própria máquina.

Para compilar: coloque o jogo em `JOGO/NPEB00553/`, gere o código seguindo o
`docs/GETTING_STARTED.md` e rode o `build-release.cmd`. Para jogar, ajuste os
caminhos no `executar-code-veronica-otimizado.cmd` e dê dois cliques nele.
Hoje o jogo chega ao menu e os vídeos tocam, mas ainda um pouco lentos.

A versão japonesa da PSN (NPJB00135) também funciona, como um segundo
executável (`MyGameRecomp_JP.exe`); veja a seção "Japanese version" acima.
Ela não precisa de arquivo .rap.


### US version and save folder

The US PSN release **NPUB30467 01.00** has its own lift and executable,
`MyGameRecomp_US.exe`, beside EU and JP. Select its game folder in the same
launcher. Set `CV_BUILD_US=1` and run `build-release.cmd` to build it;
`CV_BUILD_JP=1` updates JP in the same invocation.

The Game page also allows choosing a **Saves folder**. Leaving it unset keeps
the existing `gamedata/dev_hdd0/home/00000001/savedata` path. Changes take effect
on the next game start and do not move existing saves. Details, ELF identity,
regeneration and validation: [SAVES-E-VERSAO-AMERICANA.md](SAVES-E-VERSAO-AMERICANA.md).
