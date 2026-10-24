VERONICA PKG EXTRACTOR v1.0

Standalone extractor for Resident Evil Code: Veronica X (PS3 PSN).

HOW TO USE
1. Open VeronicaPKGExtractor.exe. No installer, terminal or Python is required.
2. Drop your .pkg file onto the window or click Browse.
3. Choose an existing writable destination folder.
4. Click Extract PKG and wait for completion.
5. Add the newly created game folder to the Veronica Port launcher.

Output example:
  <destination>\NPJB00135\PARAM.SFO
  <destination>\NPJB00135\USRDIR\EBOOT.BIN
  ... plus all the other package files and subfolders.
The folder uses the region title ID: NPJB00135, NPEB00553 or NPUB30467.

COMPATIBILITY
- Windows 10/11 x64. Administrator access is not required.
- Only retail PS3 PSN base-game PKGs, version 01.00, for the three regions above.
- PS4/PS5 packages, updates, DLC and development packages are not supported.
- CPU and disk only. Speed depends on your processor and storage.
- Portuguese and English interface, selectable in the window.

YOUR FILES
The input PKG is read-only and stays unchanged. EBOOT.BIN, EDAT and other files
are extracted as stored inside the package; the tool does not convert or decrypt
them beyond the PKG container layer. It does not create RAP files or licenses.
EU/US still require the matching RAP license in the launcher.
No game files or licenses are included with this tool.

An existing game folder is never overwritten: choose another destination.
Cancel removes incomplete extraction files. The final folder is created only
after completion. Do not unplug an external drive during extraction.
Use a regular folder without links/junctions, with enough free space.
Extract this tool from its ZIP before running it.

FIRST VERSION VALIDATION
Automated encrypted synthetic-package tests cover regions, folder structure,
empty files, invalid paths, cancellation, Unicode paths and overwrite refusal.
Extraction of a real Code Veronica PKG still needs user testing. The tool checks
headers, tables, bounds and PARAM.SFO; it does not authenticate every payload byte.

Developed by Kastiel. Format reference: the project's tools/pkg_extract.py reader.
Source code and MIT license: https://github.com/kastiellcontato-design/re-cvx-ps3recomp
