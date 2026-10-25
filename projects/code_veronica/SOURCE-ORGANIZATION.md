# Stable source and local research

The active source includes the functionality already shipped in v1.0: independently positioned/scaled SRT panels, mouse dragging, the 60 FPS VSync option, launcher cache handling, and conservative interpolation safeguards. The existing distribution wrapper still disables developer captures and profiling. This source organization does not replace the playable release or its ZIP files.

Use `distribution/README.md` for distribution builds and the normal `build-release.cmd` / `build-release-jp.cmd` developer entry points. The normal play shortcut is `executar-code-veronica-jogar.cmd`; `executar-code-veronica-otimizado.cmd` remains a compatibility entry point. Old diagnostic/comparison shortcuts have been retired. References to them in dated investigation reports describe historical tests, not current play instructions.

Unmerged Tracy, mesh discovery, native SPU/MFC, fast-loading and menu-smoothing experiments were separated from the active source. Local recovery archives are kept under the ignored `archives/` directory; they are not a prerequisite for building or playing the stable port. Original game data, locally generated lifts, SDK dependencies, caches, saves and build outputs remain separate from public source as documented by the build/setup instructions.

Validation after separation: a fresh host/runtime distribution build linked in an isolated output directory using source/hash-verified unchanged lifted objects; SRT and RECOMP SETTINGS tests passed for EU/JP/US, along with launcher-cache, pose-interpolation and updater regression tests. No gameplay session or release asset was changed during this verification.
