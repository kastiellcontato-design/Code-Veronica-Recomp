# Local PS3 trophies

The Windows launcher has a **Trophies** page in all 13 supported languages.
Recording and popup notifications can be enabled independently. The list uses
the original trophy IDs, grades and PNG icons from the selected game's own
`TROPDIR/NPWRxxxxx_00/TROPHY.TRP`; no game assets are distributed with the port.
This title's original catalog contains **12 trophies: 9 bronze, 2 silver and
1 gold, with no platinum**. It is local progress, separate from PSN/RPCN.

The package's Japanese, English, French, Spanish, German and Italian texts are
read directly. Additional Portuguese, Polish, Turkish, Arabic, Simplified
Chinese, Korean and Russian translations apply only to the identified CVX
catalog. Missing or invalid packages produce an explanatory launcher message.

## Runtime and persistence

- `sceNpTrophyUnlockTrophy` events from the game call the host backend.
  Nothing polls guest memory every frame, and merely opening the list awards
  nothing. Duplicate unlocks do not repeat notifications.
- The guest ABI uses big-endian SDK fields, the correct 1,024-byte description
  buffer, and CellRtcTick timestamps. Registration keeps the existing
  synchronous installed callback and deferred completion notification.
- Progress is saved under `gamedata/trophies/NPWRxxxxx_00.json` in the port's
  installation. Existing `bin/gamedata/trophies` layouts and the older HLE
  `NPWRxxxxx.json` filename are read without deleting or overwriting them.
- Writes use a per-path Windows mutex, reload/merge of the current progress,
  flushed temporary files and atomic replacement. Invalid progress files are
  not silently replaced.
- **Reset trophies** is available only while the game is stopped, asks for
  confirmation, and backs up existing progress before resetting it. Native
  saves, F7 save markers and `.cvsav` files are never touched by this action.
- The updater protects trophy folders, F7 markers, native save folders and
  `.cvsav` files, including organized `bin/` layouts. Personal files absent
  from the update package are left in place as before.

## Validation

`tests/test_trophies.cpp` + `tests/test_trophy_guest.c` exercise the production
TRP/XML reader and guest HLE against temporary progress storage: 13 languages,
12 original icons, grade totals, guest endianness and callbacks, unlocking,
duplicates, recording disabled, notifications disabled, reload, reset backup,
unchanged saves, legacy import, corrupt JSON and invalid archive/XML rejection.
Run `code_veronica_trophy_tests <game-directory> <new-temporary-fixture-directory>`.

The updater suite additionally installs an archive containing conflicting
trophy and save files over a temporary installation and checks that personal
data survives. Launcher rendering checks cover all 13 languages and all three
filters, navigation, scrolling, plus the previous map and update-note UI.

These automated tests do not replace a playthrough. Actual in-game achievement
conditions should still be checked in the local trophy build, starting with
the cemetery escape. Old achievements are not inferred from existing saves.

Format and ABI references consulted:

- https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Loader/TRP.h
- https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/sceNpTrophy.h

## Compact achievement notifications

Notifications now use the original trophy icon, localized name, description and grade in a compact bottom-right card. Entry takes 280 ms, the card remains visible for 5 seconds, and exit takes 300 ms. Simultaneous unlocks are queued. Disabling recording or notifications clears pending popups.

The user-supplied trophy sound is embedded as a WAVE resource, converted from 24-bit to compatible 16-bit stereo PCM at the same sample rate. A worker starts asynchronous playback once when each card begins displaying. No external audio file is needed beside the executable.

The card is rasterized once per event and uploaded once to each already-fenced frame slot. Animation updates six vertices and alpha. It uses an independent texture and descriptor from SRT and adds no GPU queue waits. Idle rendering performs no image preparation or upload.

Validation: all 12 trophies in all 13 launcher languages, entry/hold/exit timing, queue order, disable cancellation, embedded WAV playback, and GPU upload/cache/readback on WARP and the physical adapter. Automated checks do not establish an FPS delta during gameplay.

The trophy navigation icon now uses an original vector cup with curved handles,
fluted bowl, stem and stepped pedestal. Notification position is selectable
(top left, top right, bottom left, bottom right), localized in all 13 languages,
saved as [trofeus] posicao=0..3, and applied live. Default is top right.
Animation enters/exits from the corresponding horizontal side. Placement tests
cover 720p, 1080p and 4K, live changes, and INI save/reload in an isolated fixture.
