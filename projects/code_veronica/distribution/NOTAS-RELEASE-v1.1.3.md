## Português

Corrigida a tela branca ao disparar armas em cenas com muitos efeitos de iluminação. O problema podia ocorrer tanto a 30 quanto a 60 FPS. A correção foi confirmada pelo mantenedor no cenário reproduzido; não representa validação de todas as cenas do jogo.

Mantidas as correções anteriores de áudio, sombras e compatibilidade da iluminação. Esta versão não inclui otimizações experimentais de desempenho. O jogo continua usando a GPU normalmente.

Atualize pelo launcher ou extraia o pacote completo sobre a instalação. Abra **Code Veronica.exe**. Saves e configurações são preservados pelo atualizador.

## English

Fixed a white-screen rendering issue when firing weapons in scenes with many lighting effects. The issue could occur at both 30 and 60 FPS. The maintainer confirmed the fix in the reproduced scene; this does not constitute validation of every scene in the game.

Retains the previous audio, shadow and lighting compatibility fixes. Experimental performance optimizations are not included. Rendering continues to use the hardware GPU.

Update through the launcher or extract the complete package over your installation. Start **Code Veronica.exe**. The updater preserves saves and settings.

### Technical details

The raw vertex-fetch path stores per-draw state in an upload-buffer descriptor table. Its previous limit was **16,384 descriptors**. The captured failing frame contained **18,332 recorded operations**, and the log reported `descriptor table full`. When exhausted, the allocator reused its final descriptor slot, overwriting state still referenced by earlier geometry in that batch. This corrupted rendering and could turn the final image white during gunfire effects.

The descriptor budget is now **2 × MAX_DRAWS = 49,152**. Buffer layout and allocation use this shared capacity definition. Compared with v1.1.2, the table requires **16 MiB more per upload set** (32 MiB with two sets, 48 MiB with three). This remains a finite limit; the change does not claim to eliminate every possible white-screen cause.

Validation: a regression test using the actual allocator function reproduces corruption with the old capacity, checks the captured request count with the new capacity, and verifies unique state through the new table boundary. EU, US and JP distribution builds are included. The maintainer also tested the corrected diagnostic build in the affected scene. Pose interpolation, seam matching, FSR settings, lighting shaders and game timing are unchanged from v1.1.2. Developer capture tools and WARP are not included in this distribution.

Original PS3 game files are not included.
