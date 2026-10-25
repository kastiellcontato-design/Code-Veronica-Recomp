## Português

### Compatibilidade gráfica: tela preta ao usar iluminação
- Corrigido o alinhamento de envio das texturas cúbicas usadas pela iluminação, para melhorar a compatibilidade com diferentes instalações do Direct3D 12.
- O teste na **RTX 2080 Ti**, antes afetada por tela preta/travamento ao ligar o isqueiro, não voltou a apresentar a falha na sessão reportada. O participante também conseguiu atravessar portas e entrar em combate. Esse retorno é de um teste inicial; não representa validação de todas as placas ou de todas as cenas.
- Mantidas as correções de áudio da **1.1.1** e de sombras da **1.1**. Os shaders de iluminação/sombra, a interpolação e o FSR Frame Generation não foram alterados nesta atualização.

**Ainda em investigação:** contornos em alguns objetos e linhas/bordas em forma de caixa nas sombras relatados na build de teste. Esta atualização não anuncia correção desses artefatos, nem da tela branca na cena dos tiros da issue #9.

Atualize pelo launcher ou extraia o ZIP completo sobre sua instalação. Abra **Code Veronica.exe**. O atualizador preserva saves, configurações, mods e caches. As versões anteriores continuam disponíveis para retorno. Os pacotes auxiliares de LiveSplit e Veronica PKG Extractor continuam na versão 1.0.

Se o problema persistir, anexe o novo **bin/run-log.txt** à [issue #4](https://github.com/kastieloficial/RE-Code-Veronica-Recomp/issues/4) e informe a GPU e a cena afetada.

## English

### Graphics compatibility: black screens when lighting becomes active
- Fixed cube-texture upload placement to improve compatibility across Direct3D 12 installations.
- In the reported **RTX 2080 Ti** test, the previous lighter-related black screen/device-loss failure did not recur. The affected participant also passed through doors and entered combat. This was an initial test, not validation of every GPU or scene.
- Retains the **v1.1.1 audio fix** and **v1.1 shadow fixes**. Lighting/shadow shaders, pose interpolation and FSR Frame Generation are unchanged.

**Still under investigation:** object outlines and box-shaped lines/borders around shadows reported with the test build. This release does not claim to fix those artifacts or the white-screen gunfire cutscene reported in issue #9.

Update through the launcher or extract the complete ZIP over your installation. Start **Code Veronica.exe**. Saves, settings, mods and caches are preserved by the updater. Previous releases remain available for rollback. LiveSplit and Veronica PKG Extractor packages remain at v1.0.

### Technical details
The texture uploader aligned row pitch to **256 bytes** but placed consecutive cube faces at `row_pitch * row_count`. For the lighting's **1×1 cube lookup textures**, that produced face offsets `0, 256, 512, 768, 1024, 1280`. These offsets are not portable to D3D12 environments that require **512-byte subresource placement**.

The host upload-face stride is now rounded up to **512 bytes** for cube textures. Buffer allocation, CPU decode destinations and `CopyTextureRegion` source offsets use that same stride. The 1×1 cube offsets are therefore `0, 512, 1024, 1536, 2048, 2560`. The guest PS3 layout/face pitch, decoded texels, texture formats and shader calculations are unchanged. Ordinary 2D uploads and the existing 2D mip-chain path are unchanged. Cube layouts already aligned to 512 bytes retain their previous placement; a 1×1 cube requires an additional 1536 bytes of staging space. No draw passes are added, and no game-performance improvement is claimed.

The affected stable-build log recorded `DXGI_ERROR_DEVICE_REMOVED (0x887A0005)` with removal reason `DXGI_ERROR_INVALID_CALL (0x887A0001)`. The test build recorded the padded uploads and no new device-loss/PSO-creation failures in the supplied session.

The affected installation returned `E_INVALIDARG (0x80070057)` for the `D3D12_OPTIONS13` capability query. That is a failed query, **not a returned FALSE capability value**. Public reports demonstrate that RTX 2080 Ti hardware can expose unrestricted copies in other environments. This change uses the portable alignment instead of assuming that an optional relaxed-copy capability is available from the current runtime/driver. It is not an NVIDIA-only workaround.

Validation includes synthetic texture upload/readback checks on **D3D12 WARP and RX 6700 XT**, comparison against `GetCopyableFootprints`, preservation of all useful texel bytes, shadow-atlas regressions, retained audio-backend tests, and GUI/shared-log startup checks for EU/US/JP. These are targeted tests; they do not replace a full-game playthrough on every device.

References: [D3D12 placed-subresource alignment](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_placed_subresource_footprint), [CheckFeatureSupport](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-checkfeaturesupport).

Original PS3 game files are not included; provide the extracted files from your own game copy.
