## Português

Refinada a proteção de bordas claras em movimento com **FSR Frame Generation**.
Uma regra de proteção de textos podia confundir contornos claros de personagens
e objetos com texto, substituindo partes do quadro intermediário pelo quadro
atual. Agora a regra considera a correspondência do movimento com a imagem anterior.

Adicionado um **relatório leve de suporte** ao `bin/run-log.txt`: CPU/AVX2, Windows,
RAM, GPU selecionada, driver quando disponível, capacidades gráficas e configurações.
Em falhas terminais não tratadas, tenta registrar código da exceção e endereços de
pilha por módulo. Não é o coletor pesado de desempenho e não registra cada quadro
ou desenho. Ajuda a investigar relatos, mas não identifica sozinho um defeito visual.

O ajuste de bordas foi testado na AMD sem regressão visual ou perda perceptível
de desempenho no cenário observado pelo mantenedor. Ainda não há confirmação de
que resolva todos os contornos relatados em NVIDIA; não se trata de uma correção
específica de driver. Não inclui a tentativa pesada de sincronização do FSR.

Mantidas as correções anteriores de áudio, iluminação, sombras, tela branca ao
disparar e a opção de granulado de filme. Atualize pelo launcher ou extraia o pacote
completo e abra **Code Veronica.exe**. Configurações e saves não são incluídos no ZIP.

### Downloads

- Jogo/launcher: **CodeVeronica-PC-v1.1.5.zip**.
- Extrator: **Veronica-PKG-Extractor-v1.0.1.zip**, download separado nos Assets.

## English

Refined **FSR Frame Generation** handling of bright edges in motion. A conservative
text-protection rule could classify bright character/object silhouettes as text
and replace interpolated pixels with current-frame pixels. The rule now checks
whether estimated motion corresponds to matching previous-frame content.

Added a **lightweight support report** to `bin/run-log.txt`, including CPU/AVX2,
Windows, RAM, selected GPU, driver when available, graphics capabilities and effective
settings. On an unhandled terminal exception, it attempts to record the exception
and module-relative stack addresses. No heavy profiler, per-frame/per-draw tracing
or background sampling is added. This report cannot detect a visual outline by itself.

The edge adjustment showed no visible regression or noticeable performance loss
in the maintainer's tested AMD scene; this is not a controlled performance benchmark.
A fix for all NVIDIA outline reports is not confirmed. The rejected heavyweight
FSR synchronization experiment is not included.

Retains the previous audio, lighting, shadow, white-screen gunfire fixes and film
grain setting. Update through the launcher or extract the complete package and run
**Code Veronica.exe**. Original PS3 game files and personal settings are not included.
**Veronica-PKG-Extractor-v1.0.1.zip is a separate asset**, not inside the launcher ZIP.

### Technical details

This promotes the edge shader and lightweight report from FSR Edge Compatibility
Test 2. The final guard bypasses its textEdge fallback only for finite motion longer
than 0.5 pixel, temporal color change >=0.025, and previous-frame color reprojected
by the motion vector matching within 0.03 maximum normalized RGB error. Port overlay,
reset, static, invalid, stationary and out-of-range safeguards remain unchanged.
The motion texture is accessed through an additional read-only SRV; the descriptor
heap grows from 13 to 14 entries. No new image allocations, GPU copies or presentation
waits are added. Motion estimation, depth filtering and sharpening remain unchanged.
The separate async resource-lifetime concern is not resolved by this change.

Support queries run at startup, after launcher settings are applied and when the
D3D12 device is selected. The terminal handler attempts a bounded stack walk without
writing a game-memory dump. Some failure types cannot be reported, and driver version
queries may be unavailable. Logs may include normal local game paths; review before
posting publicly. No periodic telemetry, uploads or new per-frame counters are added.

Validation: seven direct GPU guard cases (bright/dark moving shapes, static/new text,
overlay, reset and untrusted motion); SDK integration in 640x360 and 1080p with capture
pixel checks, resize/reset/disable-reenable and presentation counts; lightweight
startup/AVX OS-state, synthetic terminal exception and GPU capability tests. EU/US/JP
distribution builds are included. Gameplay performance of the combined stable build
has not been benchmarked on NVIDIA.
