# Resident Evil: Code: Veronica X (PS3 Recomp) — LiveSplit

Auto start, configurable splits and game time for the native PS3 Recomp PC
port. Based on [Kapdap's Code: Veronica autosplitter](https://github.com/kapdap/re-cvx-autosplitter),
adapted for the PC port by Kastiel. The original MIT license and contributor
credits are retained; see [LICENSE-recvx.txt](LICENSE-recvx.txt).

[Instruções em português](LEIA-ME.md) · [Port project](https://github.com/kastieloficial/RE-Code-Veronica-Recomp)

## Supported executables

| Process | Release |
|---|---|
| `CodeVeronicaPort.exe` | Europe — NPEB00553 |
| `CodeVeronicaPort_JP.exe` | Japan — NPJB00135 |
| `CodeVeronicaPort_US.exe` | USA — NPUB30467 |

This script only attaches to the PC port. Use Kapdap's original script for
RPCS3, PCSX2 or Dolphin. The port executable must export `g_cv_livesplit`.

## Activate through the official LiveSplit catalog (recommended)

The port autosplitter is now included in the official catalog:
[merged PR #2964](https://github.com/LiveSplit/LiveSplit.AutoSplitters/pull/2964).

1. Open LiveSplit. To use a ready-made split list, first open the Any% or
   S-Rank `.lss` from the optional LiveSplit ZIP with **Open Splits → From File**.
2. Open **Edit Splits**. In **Game Name**, paste this exact name, including
   punctuation and the text in parentheses:

   **Resident Evil: Code: Veronica X (PS3 Recomp)**

3. Click **Activate**, then **Settings**. If the old emulator autosplitter is
   active, deactivate it before switching. If the catalog has not refreshed,
   restart LiveSplit and enter the exact name again; autocomplete is not required.
4. Enable **Start** and **Split** and check that the selected item, boss,
   event or door splits match your route. Enable **Load Remover Time** if desired.
5. In LiveSplit's main right-click menu select **Compare Against → Game Time**.
   Load removal affects Game Time; Real Time continues normally.
6. Start the PC port, then save your splits and layout.

You do not need to browse to an ASL file with this method. Use only one
autosplitter: remove a manually added **Scriptable Auto Splitter** component
from **Edit Layout** before activating the catalog version.

## What the optional ZIP adds

The ZIP includes two ready-made Any% and S-Rank `.lss` files with icons,
route settings, license/credits, and English/Portuguese instructions. Their
game name already matches the catalog entry. No ASL files are bundled:
LiveSplit downloads and loads the script automatically when you click
**Activate**. The source ASL remains in this repository for the catalog.

Stored times/history in the split lists are examples; clear them in
LiveSplit if you want to start your own history. The ZIP is optional if you
prefer to create your own split list.

Do not enable a full door route for an item-only split list. With **Load
Remover Time** enabled, Game Time pauses while the game's door flag is active.
With it disabled, Game Time uses the original in-game timer (60 ticks per
second). Door skip can shorten the paused interval. Catalog inclusion does
not establish leaderboard eligibility; consult the category moderators.

## Memory interface

The script finds `g_cv_livesplit` through the executable's PE export table;
there are no fixed host addresses or executable-size checks.

| Byte offset | Field |
|---|---|
| 0 | `PS3RECOMP-CVX` signature in a 16-byte field |
| 16 | 64-bit host pointer to guest memory |
| 24 | Release ID in a 16-byte field |
| 40 | Interface version (`1`) |

Once guest memory is initialized, the existing PS3 address tables and big-endian
readers are used. The port does not need to write timer data to a file.

## Credits and validation

- Kapdap: original autosplitter, game address tables and split logic (MIT).
- TheDementedSalad: load remover support in the original script.
- redshines: door routes and testing in the original script.
- Kastiel: PC port integration and adaptation.

Item splits and load removal were confirmed by the port maintainer in gameplay.
This is not a claim that every route or all three regional releases have been
fully tested. The adaptation retains upstream split setting identifiers so that
existing settings remain usable.
