RESIDENT EVIL CODE: VERONICA X - PC PORT (ps3recomp)

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
- Install the Visual C++ Redistributable x64 ("requisitos" folder, file
  vc_redist.x64.exe, or https://aka.ms/vs/17/release/vc_redist.x64.exe).
  If Windows says it is already installed, just close it. Without it the
  executable reports a missing MSVCP140.dll or VCRUNTIME140.dll.
- Extract the whole zip to a folder you can write to (for example
  C:\Games\CodeVeronica). Do not run it from inside the zip or from
  "Program Files". All files must stay together.


2. PREPARING THE GAME (A FOLDER, NOT A .PKG)

The port reads the game from a FOLDER. A .pkg file cannot be opened
directly: it must be installed or extracted first, like the PS4 ports that
use game folders.

a) Choose ONE method to prepare your game folder:

   OPTION 1 - VERONICA PKG EXTRACTOR (separate, optional download)
   When published, the port Release will provide two downloads:
     CodeVeronica-PC-v1.0.zip .............. the port for playing
     Veronica-PKG-Extractor-v1.0.zip ....... the PKG extraction tool
   Releases: https://github.com/kastiellcontato-design/re-cvx-ps3recomp/releases
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
   In RPCS3 it is in dev_hdd0\home\00000001\exdata, with the title ID in its
   name. If you put the .rap inside the game folder, the launcher finds it on
   its own. The Japanese version needs no .rap.
d) You do not need to decrypt anything. On the first start, EBOOT.BIN is
   decrypted into the eboot_cache folder (it takes less than a second). Your
   original files are never changed.


3. HOW TO PLAY

1. Open CodeVeronicaPort.exe (any of the three opens the same launcher).
2. On the Games page, add the folder of each version you own and, for Europe
   and USA, the .rap file if it was not found.
3. Choose the version, adjust graphics and controls, and press Play.

The launcher checks the version; another version shows a warning. The first
sessions may stutter a little while shaders are compiled. They are kept in
shader_cache and are not compiled again.


4. IN-GAME SHORTCUTS

On a controller, hold R1+R2+L1+L2 first (Xbox: RB+RT+LB+LT; Switch:
R+ZR+L+ZL), then press the last button.

  Insert / F5  (+L3)   options menu (display, audio, SRT, quit)
  F6           (+△)    item menu (enable it in Cheats)
  F7           (+○)    maps, save and load (enable it in Advanced)
  F8           (+✕)    show or hide the SRT
  F9           (+□)    mod textures on/off
  F10          (+R3)   screenshot, saved in the screenshots folder

Xbox, DualShock 4, DualSense and Switch controllers work, with vibration.
Without a controller the keyboard uses: arrows = D-pad, X = Cross,
Z = Circle, A = Square, S = Triangle, I = Start, R = Select. Everything can be
changed in the launcher.


5. TEXTURE MODS

The port replaces textures with PNGs (any size) and extracts the originals.
1. On the Mods page, use "Extract textures from all maps" (Start extraction)
   or "Dump textures from visited rooms" (while playing). The PNGs go to
   mods\texture-dumps\<version>.
2. Edit the PNGs, keeping the 16-character code at the start of the name,
   and put them in mods\textures\NPEB00553 (or NPUB30467 / NPJB00135).
3. Turn on "Replace textures". F9 turns it on and off in game.
HD packs for the PS2 version usually work, but each file must be renamed to
the PS3 code of the matching texture (see the extracted textures).


6. WHERE THINGS ARE (all inside the port folder)

- cv_launcher.ini ......... your settings
- gamedata\dev_hdd0\ ...... saves (can be changed with "Saves folder")
- shader_cache\, eboot_cache\  caches; safe to delete, they are rebuilt
- screenshots\ ............ F10 screenshots
- run-log.txt ............. log of the last session, useful to report a bug

To move your saves to another copy of the port, copy the gamedata folder.


7. IF SOMETHING GOES WRONG

- "MSVCP140.dll / VCRUNTIME140.dll is missing": install vc_redist.x64.exe.
- Antivirus software may distrust a new unsigned executable. If it deletes a
  file, extract the zip again and allow the folder.
- Send run-log.txt with a description of what happened.


Source code, credits and full instructions:
https://github.com/kastiellcontato-design/re-cvx-ps3recomp

The port is built on ps3recomp (MIT) and was developed with the help of
Claude (Anthropic) and Codex (OpenAI). Port license: LICENSE.txt. Licenses of
the libraries used (FFmpeg, ATRAC3, AMD FSR, SMAA and others): the licenses
folder and THIRD-PARTY-NOTICES.md.
