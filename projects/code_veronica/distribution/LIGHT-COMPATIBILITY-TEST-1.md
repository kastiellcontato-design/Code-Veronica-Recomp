# Light Compatibility Test 1 — based on v1.1.1

**Experimental build for black screens/freezes when using the lighter or firing a weapon. This is not a confirmed fix. Stable v1.1.1 remains the latest release; the launcher will not automatically install this test.**

This build uses portable 512-byte placement for cube faces in the GPU texture-upload buffer. It retains the v1.1.1 audio and shadow fixes and does not change lighting/shadow shaders.

Additional log entries record whether the current device/driver exposes unrestricted buffer/texture copy alignment and show the first small-cube uploads using the padded layout. Existing device-loss diagnostics are retained. These are targeted diagnostics, not a complete GPU command capture.

Public capability reports show support for unrestricted copies on both RTX 2080 Ti and RTX 5070. The affected installation still needs to be checked directly; lack of support on the RTX 2080 Ti is not an established cause. This build is a controlled compatibility experiment, not an NVIDIA-wide fix.

## How to test

1. Close the game and launcher. Keep the [stable v1.1.1 ZIP](https://github.com/kastieloficial/RE-Code-Veronica-Recomp/releases/tag/v1.1.1) for rollback.
2. Extract the entire **Code.Veronica.Lighting.Copy.Test.1.zip** over your existing port installation, replacing its files.
3. Open **Code Veronica.exe** and test the same lighter/weapon section with the same settings that previously failed. You do not need to delete saves, settings, or caches.
4. Let us know whether the issue is resolved. If it still happens, close the game and attach **bin/run-log.txt** to [issue #4](https://github.com/kastieloficial/RE-Code-Veronica-Recomp/issues/4), before reopening the game. The new session should identify **LIGHTING COPY TEST 1**.
5. To roll back, close the game and extract the stable v1.1.1 ZIP over the installation again.

EU, US and JP executables are included. No game data, saves, settings, or caches are bundled.

## Validation

- 68 synthetic D3D12 upload/readback cases passed on WARP and the same 68 on an RX 6700 XT, with all useful texture bytes preserved and no D3D12 validation errors.
- GUI startup and shared-log handoff tests passed for all three regional builds.
- Gameplay on the affected NVIDIA devices has **not** been verified. No performance improvement is claimed.

## Português

Build experimental para tela preta ou congelamento ao usar o isqueiro/disparar uma arma. Não é uma correção confirmada e não substitui a 1.1.1 estável.

Feche o jogo e extraia o ZIP completo sobre a instalação existente. Teste o mesmo trecho e avise se resolveu. Se continuar acontecendo, feche o jogo e anexe **bin/run-log.txt** na issue #4 antes de abrir o jogo novamente. O log desta build identifica **LIGHTING COPY TEST 1** e inclui a consulta de capacidade da GPU e detalhes dos uploads pequenos.

Para voltar, extraia novamente o ZIP estável da 1.1.1. As correções de áudio e sombras dessa versão foram mantidas.
