<div align="center">

<img src="projects/code_veronica/launcher_art/launcher_logo.png" alt="Resident Evil Code: Veronica X Port" width="360">

# Resident Evil Code: Veronica X — PC Port

**An unofficial native Windows port of the PlayStation 3 version, made by static recompilation.**

**English** | [Português (Brasil)](README.pt-BR.md) | [Español](README.es.md)

</div>

> [!IMPORTANT]
> **No game files are included.** This repository and its releases contain only the port: no game data, no game executable and no code generated from it. You need your **own original copy** of the PS3 game (PSN release). *Resident Evil*, *Code: Veronica* and all related names, characters and images are trademarks and copyrights of **Capcom**. This is a fan project. It is **not affiliated with, authorized or endorsed by Capcom or Sony Interactive Entertainment**.

![Gameplay](docs/images/gameplay-claire-red-room.jpg)

## Contents

- [What is this?](#what-is-this)
- [Why the PS3 version](#why-the-ps3-version)
- [Supported versions](#supported-versions)
- [Requirements](#requirements)
- [Preparing your game](#preparing-your-game)
- [Installing and playing](#installing-and-playing)
- [Features](#features)
  - [Patches](#patches)
  - [Texture mods](#texture-mods-experimental)
- [Shortcuts](#shortcuts)
- [Screenshots](#screenshots)
- [Known issues](#known-issues)
- [Building from source](#building-from-source)
- [Running the tests](#running-the-tests)
- [Repository layout](#repository-layout)
- [Credits](#credits)
- [License](#license)

## What is this?

This is a native Windows x64 port of **Resident Evil Code: Veronica X** for PlayStation 3. It is not an emulator. The game's PowerPC (PPU) and SPU code is translated ahead of time into C/C++ with [ps3recomp](https://github.com/sp00nznet/ps3recomp), then compiled into a normal Windows executable. The PS3 system libraries are reimplemented, and the RSX graphics run on Direct3D 12.

On top of that, the port adds its own launcher, modern graphics options, 60 FPS, frame generation, a speedrun-ready SRT overlay, practice tools and more. Everything is described below.

## Why the PS3 version

I am a big fan of Resident Evil and of Capcom, and Code: Veronica is one of my favourite games in the series. I chose the PS3 version because it is the one many **speedrunners** use, and I wanted them to have a fast, stable PC version with tools made for practice: health, inventory, enemies and time on screen, plus the ability to jump to any room.

The port is for **every player**: people who want to replay the game with higher resolution, 60 FPS and modern controllers, and also people who run it competitively.

## Supported versions

Only the **PSN releases, version 01.00**, are supported. Each region has its own executable; they all share the same launcher, settings, saves and caches, and each launcher starts the right executable for the chosen game folder.

| Region | Title ID | Executable | License file (.rap) |
|---|---|---|---|
| Europe | `NPEB00553` | `CodeVeronicaPort.exe` | Required |
| United States | `NPUB30467` | `CodeVeronicaPort_US.exe` | Required |
| Japan (*BIOHAZARD CODE: Veronica Kanzenban*) | `NPJB00135` | `CodeVeronicaPort_JP.exe` | Not needed |

Game updates, disc releases and other versions are not supported. The launcher checks the version and tells you if a folder is not compatible.

![Title screen of the Japanese version](docs/images/title-menu-jp.jpg)

## Requirements

- Windows 10 or 11, 64-bit.
- A graphics card with **Direct3D 12** support.
- The **Microsoft Visual C++ Redistributable (x64)**. Without it, Windows reports a missing `MSVCP140.dll` or `VCRUNTIME140.dll`.
- Your own copy of the game, as a folder (see below).

**No PS3 firmware and no RPCS3 are needed to play.**

### Test PC

The port was developed and tested on this PC, where the game was **completed from start to finish**:

| Part | Model |
|---|---|
| CPU | AMD Ryzen 5 5600X |
| GPU | AMD Radeon RX 6700 XT 12 GB |
| RAM | 32 GB DDR4 |

Other PCs should work, but this is the configuration that was actually tested.

## Preparing your game

The port reads the game from a **folder**, not from a `.pkg` file. It works like the PS4 ports that use shadPS4-style game folders: the PKG must be **installed or extracted first**. A `.pkg` file cannot be opened directly.

The folder you choose must contain `PARAM.SFO` and `USRDIR\EBOOT.BIN`:

```
NPEB00553\            (or NPUB30467 / NPJB00135)
├── PARAM.SFO
├── ICON0.PNG
└── USRDIR\
    ├── EBOOT.BIN
    └── BHCV\ ...
```

**Step by step**

1. **Get the game folder from your PKG. Choose either method:**
   - **Veronica PKG Extractor (optional, separate download):** when available, download `Veronica-PKG-Extractor-v1.0.zip` from the project's [Releases](https://github.com/kastiellcontato-design/re-cvx-ps3recomp/releases), alongside `CodeVeronica-PC-v1.0.zip`. Unzip the tool, open `VeronicaPKGExtractor.exe`, drop or select your PKG, choose a destination and click **Extract PKG**. It creates the `NPEB00553`, `NPUB30467` or `NPJB00135` folder with the original package files. Add that folder to the launcher. This tool is only for the three supported Code Veronica PSN base releases, version 01.00.
   - **RPCS3:** install your PKG using *File → Install Packages/Raps/Edats*. The game folder is at `RPCS3\dev_hdd0\game\NPEB00553` (or `NPUB30467` / `NPJB00135`). Add that folder to the launcher. RPCS3 is only needed for this preparation step.
   Both methods produce the game folder the port needs. **You only need one method.** If your game is already extracted, the extractor download is unnecessary. The launcher does not open PKGs directly, and the extractor does not create RAP licenses.
2. **Find your license (`.rap`)** for the European and American versions. It is the `.rap` file of the account that bought the game. In RPCS3 it is in `dev_hdd0\home\00000001\exdata`, with the title ID in its name (for example `EP0102-NPEB00553_00-…​.rap`). If the `.rap` file is inside the game folder, the launcher finds it on its own. **The Japanese version does not need a `.rap`.**
3. **Keep the original files as they are.** You do not need to decrypt anything: on the first start, the port decrypts `EBOOT.BIN` with your license into its own `eboot_cache` folder (it takes less than a second). Your game files are never modified.

## Installing and playing

1. Install the [Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) if you do not have it yet.
2. Extract the whole ZIP to a folder where you can write, for example `C:\Games\CodeVeronica`. Do not run it from inside the ZIP or from `Program Files`, and keep all files together.
3. Open `CodeVeronicaPort.exe` (any of the three executables opens the same launcher).
4. On the **Games** page, add your game folder for each version you own, and the `.rap` for EU/US if it was not found automatically.
5. Select a version, adjust the graphics and controls, and press **PLAY**.

The first sessions may stutter a little while shaders are compiled. They are saved in `shader_cache` and are not compiled again.

**Where things are** (all inside the port folder): settings in `cv_launcher.ini`; saves in `gamedata\dev_hdd0\` (a different folder can be chosen in the launcher); caches in `shader_cache\` and `eboot_cache\` (safe to delete, they are rebuilt); screenshots in `screenshots\`; the last session's log in `run-log.txt`, useful when reporting a problem.

The launcher can also check the GitHub releases and update itself (it can be turned off in the launcher).

## Features

### Launcher

- Its own launcher, available in **13 languages**: Português (Brasil), English, Español, 日本語, Français, Deutsch, Italiano, Polski, Türkçe, العربية, 简体中文, 한국어 and Русский.
- Game library with the three regions, version and license checks, and region flags.
- Stays open during play as the **in-game options menu** (`Insert`/`F5`), so settings can be changed without leaving the game.
- Pages: Games, Graphics, Saves & captures, Controls, Shortcuts, Patches, Mods, SRT, Cheats, Performance and Advanced (language, shader cache, live game log viewer).
- Optional pause when the game window loses focus.

### Graphics and performance

- **Render resolution**: 480p, native 720p, 1080p, 1440p and **4K** (3840×2160). Windowed, borderless window or fullscreen.
- **Anti-aliasing**: FXAA (light) or SMAA (best).
- **Anisotropic filtering**: 2x, 4x, 8x and 16x.
- **Sharpening** with **AMD FSR 1 RCAS**, from 10% to 100%.
- **Frame limit**:
  - **30 FPS (original)**: the same experience as the PS3, where the game runs at 30 FPS.
  - **60 FPS**: the game logic stays at its original 30 Hz and the extra frames come from **interpolating character and object poses**, so the timing of the game is kept. Interpolation runs on the CPU by default, with an experimental GPU mode. If the PC cannot hold 60 FPS, the game runs slower.
- **AMD FSR 3.1 Frame Generation** (optional, 60 FPS mode only): up to 120 FPS when the PC holds 60 FPS.
- **VSync** (60 FPS mode): no tearing. It cannot be used together with frame generation.
- Performance overlay with FPS, frame time, CPU, GPU, RAM, VRAM and SPU/RSX load, in any corner.
- Persistent shader and pipeline cache, loaded in the background, with optional progress note.
- Movies (FMV) decoded with FFmpeg and ATRAC3plus audio decoded natively: no PS3 firmware needed.

### SRT for speedrunners

| | |
|---|---|
| ![SRT with the Albinoid boss bar](docs/images/srt-albinoid-chris.jpg) | ![SRT during the Tyrant fight](docs/images/srt-tyrant-claire.jpg) |
| ![Gulp Worm boss bar](docs/images/srt-gulp-worm-bar.jpg) | ![SRT panel with a boss](docs/images/srt-panel-boss.png) |

A native **SRT** (speedrun tool) overlay drawn by the game itself, toggled with `F8`:

- Character name and portrait, **health** (value, percentage and status: fine, caution, danger, poisoned, gas).
- **In-game time**, saves, retries and first-aid sprays (F.A.S.).
- **Enemies** with health bars and portraits, and **boss health bars** (in the panel, or centered at the top or bottom of the screen).
- **Inventory** with the equipped weapon, ammo and the game's own item icons, read from your game files.
- Any corner, row or column layout, inventory beside or below, size from 50% to 200%, background opacity, border, panel language, or a **separate window** you can place anywhere (for example on a second monitor or in OBS).
- A **LiveSplit** autosplitter that also attaches to the port is included in `projects/code_veronica/livesplit/`.

### Practice tools, saves and items

- **Archives** window (`F7`, enabled in Advanced): **load any room or map** (with the room's entry point) to practice a section, **save anywhere** into an independent save library with thumbnails, and **load** those saves at any time.
- **Item menu** (`F6`, enabled in Cheats): put any item or weapon in the current character's inventory, or take one out, without leaving the game.
- **Cheats**: infinite health and infinite ammo, switchable during play.
- **Patches**: skip door animations, skip the opening logos and intro, plus your own patches in RPCS3's `patch.yml` format.
- Choose where saves are stored.

### In-game RECOMP SETTINGS and character swap

![RECOMP SETTINGS with the appearance selector](docs/images/recomp-settings-appearance.jpg)

In the game's own **OPTION** menu, the **RECOMP SETTINGS** page changes resolution, anti-aliasing, display mode, brightness, contrast, saturation, sharpening (FSR RCAS) and anisotropic filtering without opening the launcher, with **Reset colors** to go back to the defaults.

At the top of the same page, **APPEARANCE** lets you **play as another character model**: press left/right to choose and see it in a rotating **360° preview**. There are 33 options: Claire (original and alternate outfit), Chris, Steve, Wesker, 26 zombie variants and two forms of Alexia (first form and human). The chosen look is also applied when you play as Chris, Steve or Wesker. It only changes how the character looks; the story, the controls and your save stay the same.

### Controls

- **Xbox** (XInput), **PlayStation** (DualShock 4 and DualSense) and **Nintendo Switch** controllers, plus keyboard, all at the same time.
- **Vibration** with an adjustable strength (0–200%).
- Any key or button can be remapped, with several keys per action.
- On-screen button icons for PlayStation, Xbox, Switch or keyboard, chosen automatically from the last device used, in HD.

### Patches

The **Patches** page lists changes to the game's own code. They apply at once, even while the game is running.

- **Skip door animations**: the next room loads right away, without the door-opening animation (by Kapdap, from the RPCS3 patch list).
- **Skip logos and intro**: the game opens straight on the title screen, without the CAPCOM / MT FRAMEWORK logos and the intro movie.
- **Your own patches**: choose a folder with RPCS3 `.yml` patch files (the same format as RPCS3's `patch.yml`). The patches for your game version are listed with their own switch. Patches that would change code this build was not compiled with are shown as unavailable.

### Texture mods (experimental)

The port can **replace the game's textures with PNG images** and can also **extract the original textures** for you, already with the right names. Everything is on the **Mods** page of the launcher.

**How the names work.** Every texture has a code: 16 hexadecimal characters at the start of the file name, for example

```
1A2B3C4D5E6F7081_512x256_DXT5.png
└──── code ────┘ └size┘ └format┘
```

Only the **code** matters. A replacement PNG can have **any size** (a 512×256 texture can become 2048×1024 for HD) and the rest of the name is free, but it must **start with the same code**. Only `.png` files are read.

**Step 1 — Extract the textures** (to know the code of each one)

1. Open the launcher and go to **Mods**.
2. Choose an extraction mode:
   - **Extract textures from all maps**: reads the game files without playing. Select it and click **Start extraction**. It takes a few minutes, shows its progress and can be canceled. A CSV file lists which game file each PNG came from.
   - **Dump textures from visited rooms**: turn it on and play; every new texture that appears (rooms, characters, items, effects) is saved. Use it for images the game creates while running, which are not stored in the files.
3. The PNGs go to the **Extracted textures folder** (by default `mods\texture-dumps\<version>` next to the executable). Existing PNGs are never overwritten, so your edits are safe.

**Step 2 — Make the mod**

1. Copy the PNGs you want to change and edit them (or upscale them) in any image editor.
2. **Keep the code at the start of the name.** You may change the size.
3. Put them in the **Mod textures folder** (by default `mods\textures`), inside the subfolder of your version: `NPEB00553`, `NPUB30467` or `NPJB00135`. Without a subfolder, the folder itself is used for all versions. Subfolders inside it are allowed, so you can organize the pack as you like.

**Step 3 — Use it**

1. Turn on **Replace textures** on the Mods page (restart the game if you changed the mod folder).
2. In game, press **F9** (controller: R1+R2+L1+L2+□ / RB+RT+LB+LT+X) to compare the mod with the original textures at any time.
3. Big packs do not cost memory up front: each PNG is only loaded when its texture appears on screen.

> [!TIP]
> **Using PS2 HD texture packs.** HD texture packs made for the PS2 version of Code: Veronica usually work, because the textures are the same, but **their file names are different**: each file must be **renamed to the PS3 code** of the matching texture. Extract the textures with **Extract textures from all maps**, find the extracted PNG that shows the same image as the PS2 file, and rename the PS2 file with that PNG's name (at least its 16-character code). Then put it in the mod folder as in Step 2.

Notes: cube-map faces (reflections) are extracted to `reference\cubemaps` for reference only; replacement currently supports 2D textures. Never share the extracted original textures; share only your own edited images.

## Shortcuts

On a controller, hold the four shoulder buttons first, then press the last button.

| Keyboard | PlayStation | Xbox | Switch | Action |
|---|---|---|---|---|
| `Insert` / `F5` | R1+R2+L1+L2+L3 | RB+RT+LB+LT+LS | R+ZR+L+ZL+LS | Options menu (display, audio, SRT, quit) |
| `F6` | R1+R2+L1+L2+△ | RB+RT+LB+LT+Y | R+ZR+L+ZL+X | Item menu (enable it in Cheats) |
| `F7` | R1+R2+L1+L2+○ | RB+RT+LB+LT+B | R+ZR+L+ZL+A | Archives: maps, save, load (enable it in Advanced) |
| `F8` | R1+R2+L1+L2+✕ | RB+RT+LB+LT+A | R+ZR+L+ZL+B | Show or hide the SRT |
| `F9` | R1+R2+L1+L2+□ | RB+RT+LB+LT+X | R+ZR+L+ZL+Y | Mod textures on/off |
| `F10` | R1+R2+L1+L2+R3 | RB+RT+LB+LT+RS | R+ZR+L+ZL+RS | Screenshot (saved in `screenshots\`) |

Default keyboard layout without a controller: arrows = D-pad, `X` = Cross, `Z` = Circle, `A` = Square, `S` = Triangle, `I` = Start, `R` = Select. Everything can be changed on the Controls page.

## Screenshots

| | |
|---|---|
| ![Launcher graphics](docs/images/launcher-graphics.png) | ![Shortcuts](docs/images/launcher-shortcuts.png) |
| ![Controls](docs/images/launcher-controls.png) | ![Cheats and item menu](docs/images/launcher-cheats.png) |
| ![Music box room](docs/images/gameplay-music-box-room.jpg) | ![Hunter](docs/images/gameplay-hunter-armory.jpg) |
| ![Tyrant](docs/images/gameplay-tyrant-countdown.jpg) | ![Zombies](docs/images/gameplay-zombies.jpg) |
| ![Chris](docs/images/gameplay-chris.jpg) | ![Japanese version](docs/images/gameplay-inventory-jp.jpg) |
| ![SRT settings](docs/images/launcher-srt.png) | |

## Known issues

The port is a work in progress. Graphics and lighting glitches can still appear, 60 FPS is not guaranteed on every PC, and most of the gameplay testing has been done on the Japanese version (Europe and America share the same code). Please report problems with your `run-log.txt` and a description of what happened.

## Building from source

Building is only needed if you want to work on the port. It requires your own decrypted game, because the recompiled game code is generated on your machine and is **never committed**.

**Tools:** Visual Studio 2022 or newer (or Build Tools) with the C++ workload, **LLVM/clang-cl**, CMake 3.20+, Ninja, Python 3.12, and the FFmpeg 7.1 shared DLLs (`avcodec-61`, `avutil-59`, `swscale-8`, `swresample-5`) in `third_party/ffmpeg/bin/`.

1. **Lift the game.** Follow [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) and [docs/SPU_LIFTER.md](docs/SPU_LIFTER.md) to generate the PPU code in `src/recomp/` (EU), `src/recomp_us/` (US) and `src/recomp_jp/` (JP), and the SPU images in `projects/code_veronica/spu/`. For example, for Japan:
   ```
   python tools/ppu_loader.py <NPJB00135>/USRDIR/EBOOT.elf -o out_jp/
   python tools/ppu_lifter.py <NPJB00135>/USRDIR/EBOOT.elf --functions out_jp/EBOOT.functions.json --hle-stubs out_jp/EBOOT.imports.json --patches projects/code_veronica/patches.json -o src/recomp_jp
   ```
2. **Build.** Run `projects/code_veronica/build-release.cmd` (Europe). Set `CV_BUILD_JP=1` and/or `CV_BUILD_US=1` to build the other regions in the same run, or use `build-release-jp.cmd` for Japan alone. The scripts contain the author's paths (Visual Studio, LLVM and `D:/ps3recomp`); adjust them to your machine.
3. The executables are written to `projects/code_veronica/build-release/bin/`. Always use the Release build: the Debug build is far too slow for the recompiled code.
4. `projects/code_veronica/empacotar-release.cmd` builds the release ZIP (port files only, never game data).

## Running the tests

The tests run on demand through `build-release.cmd`, using environment variables. They use synthetic data and temporary folders, never your saves or settings. Examples:

```
set CV_SRT_TESTS=1
set CV_UPDATER_TESTS=1
projects\code_veronica\build-release.cmd
```

| Variable | What it checks |
|---|---|
| `CV_SRT_TESTS` | SRT memory reading, layout and controller shortcut gate (EU, plus JP/US with `CV_BUILD_JP`/`CV_BUILD_US`) |
| `CV_MAP_TESTS` | Map/room tools and dispatch |
| `CV_ITEMS_TESTS` | F6 item menu rules and guest writes, per region |
| `CV_CHEAT_TESTS` | Cheats in the three regions |
| `CV_SAVE_TESTS` | Save library, in isolated folders |
| `CV_MENU_TESTS` | In-game RECOMP SETTINGS menu navigation |
| `CV_FPS_TESTS`, `CV_POSE_TESTS`, `CV_POSE_GPU_TESTS` | 30/60 FPS clock and pose interpolation |
| `CV_UPDATER_TESTS` | Launcher updater, offline |
| `CV_FOCUS_PAUSE_TESTS` | Pause on focus loss |
| `CV_FP_TESTS`, `CV_DXC_TESTS`, `CV_SHADOW_FILTER_TESTS` | Shader translation and filters |
| `CV_SPU_FAULT_TESTS`, `CV_SPU_POOL_TESTS`, `CV_SPU_SCHEDULE_TESTS` | SPU runtime |

Other test targets (for example `code_veronica_launcher_cache_tests`) are listed in `projects/code_veronica/CMakeLists.txt` and can be built with `cmake --build build-release --target <name>`.

## Repository layout

| Path | Contents |
|---|---|
| `projects/code_veronica/` | The port: launcher, SRT, tools, renderer hooks, build scripts and design notes |
| `projects/code_veronica/livesplit/` | LiveSplit autosplitter with port support |
| `runtime/`, `libs/`, `tools/`, `include/` | The ps3recomp runtime, PS3 library reimplementations and recompiler tools |
| `third_party/` | FFmpeg, ATRAC3 decoder, SMAA and AMD FSR 3.1 |
| `docs/` | ps3recomp documentation and the images of this README |
| [README-ps3recomp.md](README-ps3recomp.md) | The original ps3recomp README |

## Credits

**Port:** made by **Kastiel**.

**Development assistance:** the port was developed with the help of AI assistants: **Claude** (Anthropic) and **Codex** (OpenAI).

**Foundation**
- [ps3recomp](https://github.com/sp00nznet/ps3recomp) by **sp00nznet (Ned Heller)** and its [contributors](CONTRIBUTORS.md): the static recompilation toolkit and runtime this port is built on (MIT).
- [RPCS3](https://rpcs3.net): the PS3 emulator, used as the reference for hardware and system behaviour, for frame-by-frame comparisons and for its `patch.yml` format.

**Libraries and assets**
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1 or later): MPEG-2 movie decoding.
- ATRAC3/ATRAC3plus decoder from FFmpeg, as extracted by [PPSSPP](https://github.com/hrydgard/ppsspp) (`at3_standalone`, LGPL 2.1 or later).
- [AMD FidelityFX FSR 3.1](https://gpuopen.com/fidelityfx-super-resolution-3/) frame generation runtime (AMD license) and AMD FSR 1 RCAS/EASU.
- [SMAA](https://www.iryoku.com/smaa/) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez (MIT).
- FXAA 3.11 by Timothy Lottes (NVIDIA).
- [stb_image and stb_image_write](https://github.com/nothings/stb) by Sean Barrett (public domain / MIT).
- [GitHub Octicons](https://github.com/primer/octicons) (MIT), for the GitHub mark in the launcher.

**Resident Evil community**
- **Kapdap**: [re-cvx-srt-provider](https://github.com/kapdap/re-cvx-srt-provider) (MIT), whose memory addresses and game-state rules were studied for the SRT; the [Code: Veronica autosplitter](https://github.com/kapdap/re-cvx-autosplitter) (MIT) that the included LiveSplit script extends; and the door-skip patch from the RPCS3 patch list.
- **aikatore and contributors**: [recvx-enhanced-port](https://github.com/aikatore/recvx-enhanced-port) (MIT), for room location labels and as a reference for in-between frames.

**Capcom** created *Resident Evil Code: Veronica*. Please support the official releases.

The full license texts are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## License

The port code and ps3recomp are released under the **MIT License** — see [LICENSE](LICENSE). Third-party components keep their own licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The license does not cover the game: *Resident Evil Code: Veronica X* and its assets belong to Capcom, and you must own the game to use this port.
