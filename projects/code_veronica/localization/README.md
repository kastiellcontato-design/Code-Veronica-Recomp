# Portuguese (Brazil) localization

European NPEB00553 and American NPUB30467 only. Original is the default.

The `.cvp` files contain bounded COPY/literal differences, not complete room archives. The runtime verifies SHA-256 of the base and result and reconstructs a private temporary cache. Original game files are never overwritten. `sysmes.msb` contains translated text records. `ptbr-textures/` contains the approved text images and font; inventory atlas replacements are excluded.

Build the distribution, then run `tools/empacotar_release.py --distribution --version v1.1.7`. The localization directory is included automatically. Keep the root entry point beside `bin/` and `assets/`.

Offline regression: `code_veronica_translation_patch_tests <original-title-folders> <localization-directory> <reference-translated-archives>` verifies all 410 patches, corruption rejection and cache reconstruction. The reference files are test fixtures generated locally from the user's game, not distributed.
