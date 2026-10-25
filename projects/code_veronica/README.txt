NEW IN v1.1
Fixed missing or distorted lighter shadows and incorrect dark lighting blocks.

PACKAGE LAYOUT

  Code Veronica.exe       Open this file to play.
  bin/                    Regional executables and internal dependencies.
  LEIA-ME.txt / README.txt Portuguese and English instructions.

You do not need to open the EXEs inside bin. The launcher selects the region.
The port keeps settings, saves, mods, exported textures, screenshots and
caches in bin beside the regional executables. Relative paths in this guide
start there. The log is bin/run-log.txt. Keep the extracted folder structure;
do not move individual files out of bin or run the game inside the ZIP.
To migrate from the old distribution, close the game and copy your personal
files (cv_launcher.ini, saves, mods and caches) into bin, keeping the new EXEs
and DLLs. Keep a backup of the old installation.

RESIDENT EVIL CODE: VERONICA X - PC PORT v1.1

An unofficial Windows port of the PS3 version, made by static
recompilation. Made by Kastiel.

NO GAME FILES ARE INCLUDED IN THIS PACKAGE. You need your own original copy
of the PS3 PSN release. Resident Evil and Code: Veronica are trademarks of
Capcom. This is a fan project, not affiliated with, authorized or endorsed by
Capcom or Sony.

Supported versions (only version 01.00 of each):

  NPEB00553  Europe       -> CodeVeronicaPort.exe     (needs the .rap license)
  NPUB30467  USA          -> CodeVeronicaPort_US.exe  (needs the .rap license)
  NPJB00135  Japan        -> CodeVeronicaPort_JP.exe  (no .rap needed)

No PS3 firmware and no RPCS3 are needed to play.


1. BEFORE THE FIRST START

- Windows 10 or 11, 64-bit, with a DirectX 12 graphics card.
- An x86-64 CPU with AVX2 support. This build does not support CPUs without AVX2.
- An up-to-date graphics driver. Memory usage and performance depend on the
  resolution and 30/60 FPS mode; 60 FPS is not guaranteed on every PC.
- Install the Visual C++ Redistributable x64 (vc_redist.x64.exe):
  https://aka.ms/vs/17/release/vc_redist.x64.exe
   The installer is not bundled in this ZIP; use the official link above.
   If Windows says it is already installed, just close it. Without it the
  executable reports a missing MSVCP140.dll or VCRUNTIME140.dll.
- Extract the whole zip to a folder you can write to (for example
  C:\Games\CodeVeronica). Do not run it from inside the zip or from
  "Program Files". Keep the extracted folder structure.


2. PREPARING THE GAME (A FOLDER, NOT A .PKG)

The port reads the game from a FOLDER. A .pkg file cannot be opened
directly: install or extract it first with a PS3-compatible tool.

a) Choose ONE method to prepare your game folder:

   OPTION 1 - VERONICA PKG EXTRACTOR (separate, optional download)
   When published, the port Release will provide two downloads:
     Code.Veronica.Recomp.Release.v1.1.zip .............. the port for playing
     Veronica-PKG-Extractor-v1.0.zip ....... the PKG extraction tool
   Releases: https://github.com/kastiellcontato-design/Code-Veronica-Recomp/releases
   Unzip the tool and open VeronicaPKGExtractor.exe.
   Drop or select your PKG, choose a destination and click Extract PKG.
   The tool creates the title-ID folder with all the original package files.
   Add that folder on the launcher's Games page.
   Only the three PSN base releases, version 01.00, in this guide are supported.

   OPTION 2 - RPCS3
   Install your .pkg using File > Install Packages/Raps/Edats.
   The folder is at RPCS3\dev_hdd0\game\NPEB00553 (or NPUB30467 / NPJB00135).
   Official download: https://rpcs3.net/download
   Guide: https://rpcs3.net/quickstart
   Add that folder on the launcher's Games page.

   Use just one method. If you already have the game folder, skip the extractor.
   The launcher does not open PKGs directly. The extractor creates no RAP licenses.
   RPCS3 is not needed to play after preparing the game folder.

b) The right folder is the one with PARAM.SFO and USRDIR\EBOOT.BIN.
c) Europe and USA: find the .rap file of the account that bought the game.
   In RPCS3 it is usually in dev_hdd0\home\00000001\exdata. Use the license
   matching your game and selected region. If you put the .rap inside the game folder, the launcher finds it on
   its own. The Japanese version needs no .rap.
d) If extraction creates an extra parent folder, open it until PARAM.SFO and
   USRDIR are next to each other. Select that folder in the launcher:

   NPEB00553\              (or NPUB30467 / NPJB00135)
     PARAM.SFO
     USRDIR\
       EBOOT.BIN
       BHCV\ ...

e) No manual executable decryption is required. On first start the launcher
   prepares EBOOT.BIN in eboot_cache, using your license when required. Your
   original files stay intact. An already decrypted EBOOT.elf may also be
   recognized when it matches the supported game version exactly.
   Renaming .pkg to .elf does not extract or convert a package.
f) Updates, disc releases and other title IDs are not supported by this build.
   Use the base PSN game, version 01.00, from the table above.


3. HOW TO PLAY

1. Open Code Veronica.exe in the main folder. Select your release in the
   launcher and click Play once.
   Open the EXE directly: no CMD, BAT, Python or Ghidra is required.
   Distribution executables do not create a terminal window.
2. On the Games page, add the folder of each version you own and, for Europe
   and USA, the .rap file if it was not found.
3. Choose the version, adjust graphics and controls, and press Play.

The launcher checks the version; another version shows a warning. The first
sessions may stutter a little while shaders are compiled. They are kept in
shader_cache to reduce recompilation in later sessions.


4. IN-GAME SHORTCUTS

On a controller, hold R1+R2+L1+L2 first (Xbox: RB+RT+LB+LT; Switch:
R+ZR+L+ZL), then press the last button.

  Insert / F5  (+L3)   options menu (display, audio, SRT, quit)
  F6           (+△)    item menu (enable it in Cheats)
  F7           (+○)    maps, save and load (enable it in Cheats)
  F8           (+✕)    show or hide the SRT
  F9           (+□)    mod textures on/off
  F10          (+R3)   screenshot, saved in the screenshots folder

F12 does not start technical captures in this distribution. The developer
motion capture bound to F9 has also been removed. F9 only toggles the player's
texture mods; F10 remains the ordinary screenshot feature.

Xbox, DualShock 4, DualSense and Switch controllers work, with vibration.
Without a controller the keyboard uses: arrows = D-pad, X = Cross,
Z = Circle, A = Square, S = Triangle, I = Start, R = Select. Everything can be
changed in the launcher.


5. TEXTURE MODS

The port supports PNG and DDS replacements and exports originals as PNG or DDS.
Choose the export format on the Mods page before extracting. Supported DDS
formats include BC1/DXT1, BC2/DXT3, BC3/DXT5, BC7 and common RGB/RGBA formats,
with transparency. Premultiplied alpha (including DXT2/DXT4), cubemaps, arrays
and HDR are unsupported; convert to a supported format. BC7 dimensions must
be multiples of 4. DDS takes priority when PNG and DDS share the same code.
1. On the Mods page, use "Extract textures from all maps" (Start extraction)
   or "Dump textures from visited rooms" (while playing). The files go to
   mods\texture-dumps\<version>.
2. Edit the PNGs or DDS, keeping the 16-character code at the start of the name,
   and put them in mods\textures\NPEB00553 (or NPUB30467 / NPJB00135).
3. Turn on "Replace textures". F9 turns it on and off in game.
HD packs for the PS2 version usually work, but each file must be renamed to
the PS3 code of the matching texture (see the extracted textures).


CHARACTER APPEARANCE

Previews and appearances use models and textures from your own game files.
The first time you select a character, the port prepares the required resources
and stores them in appearance_cache beside the executable. The first selection
may take a little longer; later selections reuse the cache.
Python and development tools are not required. Your original game files stay
unchanged. A missing optional asset does not block the other characters;
see run-log.txt for the specific error.
The cache is separated by game installation and language, rebuilt if incomplete,
and preserved by the updater. It contains no saves and is not included in this ZIP.


6. WHERE THINGS ARE (all inside the port folder)

- cv_launcher.ini ......... your settings
- gamedata\dev_hdd0\ ...... saves (can be changed with "Saves folder")
- shader_cache\, eboot_cache\  rebuilt if deleted (this can cause recompilation
  and temporary stutters; avoid clearing them unnecessarily)
- screenshots\ ............ F10 screenshots
- run-log.txt ............. log of the last session, useful to report a bug

Test CMDs, rsxcap/motion-captures and development profilers are not included.
The normal error/session log is retained. When reporting a problem, attach
that log and describe your CPU, GPU, game region and where the issue happened.

To move your saves to another copy of the port, copy the gamedata folder.


7. IF SOMETHING GOES WRONG

- "MSVCP140.dll / VCRUNTIME140.dll is missing": install vc_redist.x64.exe.
- Check that the ZIP was fully extracted and the DLLs remain beside the EXEs.
- Version warning: check the title ID, version 01.00 and your EU/US .rap license.
- Immediate exit: check CPU AVX2 support, graphics driver and Visual C++ x64.
- This first build may still have graphics or performance issues in scenarios
  not validated across every region and hardware configuration.
- Send run-log.txt with a description of what happened.


Source code, credits and full instructions:
https://github.com/kastiellcontato-design/Code-Veronica-Recomp

The port is built on ps3recomp (MIT) and was developed with the help of
Claude (Anthropic) and Codex (OpenAI). Port license: LICENSE.txt. Licenses of
the libraries used (FFmpeg, ATRAC3, AMD FSR, SMAA and others): the licenses
folder and THIRD-PARTY-NOTICES.md.

This package contains no game, PKG, RAP license, firmware, saves or developer
personal settings. Do not add those files when redistributing the port ZIP.
The code is provided under the included licenses, without warranty.
