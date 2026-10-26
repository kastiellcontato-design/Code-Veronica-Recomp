## Português

Adicionada a opção **Granulado de filme** na aba gráfica do launcher e **Film grain** no Recomp Config dentro do jogo. Você pode ligar ou desligar o efeito. A opção começa ligada, mantendo o visual original.

**Reinicie o jogo para aplicar a alteração.** Os dois menus compartilham a mesma configuração salva. O launcher tem o texto traduzido nos 13 idiomas disponíveis; dentro do jogo, a nova opção e o aviso de reinício aparecem em inglês.

Mantidas as correções anteriores de áudio, iluminação, sombras e tela branca ao disparar. Esta atualização não inclui os experimentos privados de desempenho ou as ferramentas de diagnóstico.

Atualize pelo launcher ou extraia o pacote completo sobre a instalação e abra **Code Veronica.exe**. O atualizador preserva saves e configurações.

## English

Added an optional **Film grain** setting to the launcher's graphics page and the in-game Recomp Config menu. Turn it on or off to choose the image style you prefer. It is enabled by default to preserve the original appearance.

**Restart the game to apply changes.** Both menus share the same saved preference. The launcher label and restart notice are localized in all 13 supported languages; the new in-game option and restart notice use English.

Retains the previous audio, lighting, shadow and white-screen gunfire fixes. Private performance experiments and diagnostic tools are not included.

Update through the launcher or extract the complete package over your installation, then start **Code Veronica.exe**. Saves and settings are preserved by the updater.

### Technical details

The setting persists as `[tela] filtro_filme` in `cv_launcher.ini`. The requested preference is separate from the state latched at game startup, so switching it in a menu does not partially invalidate cached resources during a running session.

When disabled, the Code Veronica VFS overlay makes only the six `TVMask`, `TVNoise` and `VolumeNoise` textures (`.tex` and `_LIN.tex`) under `USRDIR/BHCV/nativePS3/system/filter` unavailable to guest file lookups. Stat/Open resolve consistently through the overlay. No source game files are deleted, moved or edited. This disables that texture-based filter set, including its mask; it is not a global post-processing or shader disable switch. Files previously removed manually must be restored to re-enable the original effect.

Validation: the maintainer confirmed the effect removal in the private build. Automated tests cover EU/US/JP paths, restart-latched behavior, off/on restoration, exclusion of unrelated files, menu input, translated launcher strings, fixed English in-game labels and notices, menu bounds and isolation from Reset Colors. EU, US and JP distribution builds are included.

Original PS3 game files are not included.
