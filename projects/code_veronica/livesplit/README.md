# Resident Evil: Code: Veronica X (PS3 Recomp) — LiveSplit

Auto start, configurable splits and game time for the native PS3 Recomp PC
port. Based on [Kapdap's Code: Veronica autosplitter](https://github.com/kapdap/re-cvx-autosplitter),
adapted for the PC port by Kastiel. The original MIT license and contributor
credits are retained; see [LICENSE-recvx.txt](LICENSE-recvx.txt).

[Instruções em português](LEIA-ME.md) · [Port project](https://github.com/kastiellcontato-design/Code-Veronica-Recomp)

## Supported executables

| Process | Release |
|---|---|
| `CodeVeronicaPort.exe` | Europe — NPEB00553 |
| `CodeVeronicaPort_JP.exe` | Japan — NPJB00135 |
| `CodeVeronicaPort_US.exe` | USA — NPUB30467 |

This script only attaches to the PC port. Use Kapdap's original script for
RPCS3, PCSX2 or Dolphin. The port executable must export `g_cv_livesplit`.

## Install now

1. Download [recvx-ps3recomp.asl](https://raw.githubusercontent.com/kastiellcontato-design/Code-Veronica-Recomp/code-veronica/projects/code_veronica/livesplit/recvx-ps3recomp.asl).
2. In LiveSplit, open **Edit Layout → + → Control → Scriptable Auto Splitter**.
3. Open that component's settings, browse to the ASL file, enable **Start** and
   **Split**, and select the item, boss, event or door splits for your route.
4. Select **Compare Against → Game Time** from LiveSplit's right-click menu.
5. Start the port, then save your layout to preserve your settings.

Use only one autosplitter. Deactivate the emulator autosplitter in **Edit Splits**
when using this component. Existing split files and their icons can still be
used, but their selected events must match the intended route. Full door routes
are optional; do not enable one for an item-only split list.

With **Load Remover Time** enabled, Game Time pauses while the game's door flag
is active. Real Time continues normally. With it disabled, Game Time uses the
original in-game timer (60 ticks per second). A door-skip patch can shorten the
paused interval.

## LiveSplit catalog submission

The proposed game name is **Resident Evil: Code: Veronica X (PS3 Recomp)**.
Catalog availability is pending maintainer approval. After approval, enter that
exact name in **Edit Splits → Game Name**, click **Activate**, then **Settings**.
Use this catalog installation instead of adding a second manual component.
Search suggestions and speedrun leaderboard eligibility are separate matters.

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
