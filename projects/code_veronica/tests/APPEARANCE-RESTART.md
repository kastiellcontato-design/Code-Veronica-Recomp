# Appearance texture rebinding after a player reload

A cached appearance retained its relocated model and `ready` flag across a
player load. `frame()` installed that cached model without repeating the
engine's TEXLIST-to-DDS registration. If those associations had been cleared,
the model could be attached without its material bindings.

`install()` now resolves the retained alternate archive's resource list and
registers the base bank and both expression banks before changing the actor.
A separate texture-binding flag is invalidated for every cached visual by
`before_player_load()`. Freshly realized banks and same-load cached switches
are not registered twice.
The original engine archive slot is restored immediately after the query.
A failed lookup or registration keeps the native model and reports failure.
Model relocation and geometry conversion are not repeated.

Regression coverage: `tests/test_appearance_restart.cpp` includes the actual
runtime with guest-memory and engine-call test doubles. It tests cached skins
on Chris, registry clearing, player reload, then Claire; base/expression banks,
archive-slot preservation, ordinary restoration and failed registrations.
It passed for JP, US and EU. The previous runtime fails the texture-registration
assertion. This is a simulated registry reset, not a reproduction inside the
running game; the user also confirmed that the corrected development build resolved the visual issue.

The current development executables under `build-release/bin` were relinked
with only the appearance-runtime object changed. Previous objects/executables
are backed up in the Codex workspace `work/appearance-restart-fix/before`.
The v1.0 public distribution includes this correction. Menu-smoothing/loading
experiments are excluded and save files are not bundled. No in-game execution
was performed by the agent for validation.
