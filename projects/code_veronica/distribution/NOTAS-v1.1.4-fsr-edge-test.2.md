# FSR Edge Compatibility Test 2

**Experimental pre-release based on v1.1.4.** This build is intended for users
reporting bright outlines or unstable character edges with FSR Frame Generation,
including reports on NVIDIA GPUs. A fix on the affected hardware is not yet confirmed.

## Download and test

1. Download `CodeVeronica-PC-v1.1.4-fsr-edge-test.2.zip` and extract the complete
   package into a separate folder. Launch `Code Veronica.exe`.
2. Select your game files and enable 60 FPS plus **FSR Frame Generation** in the
   graphics settings. The package contains no personal settings or game files.
3. Revisit the same scene, camera angle, resolution and sharpening settings where
   the outline occurred. Check the character in motion, subtitles and overlays.
4. Please report whether the outline improved, stayed the same or became worse,
   and whether real FPS changed. Include your GPU/driver, a short video link if
   possible, and `bin/run-log.txt` from this test build after closing the game.

This supersedes Test 1, adding lightweight support logs with the same edge-handling shader.

The launcher still displays base version v1.1.4; the package/tag and the log marker
`[SUPPORT-BUILD] v1.1.4-fsr-edge-test.2` identify this experimental build. It is a manual
test download; it does not replace the latest stable release.

**Veronica Extractor is a separate asset:** `Veronica-PKG-Extractor-v1.0.1.zip`.
It is not inside the port/launcher ZIP.

## Technical change

The final conservative guard previously treated a bright/high-contrast scene
silhouette as a text edge and could replace correctly interpolated pixels with
pixels from the current real frame. The guard now bypasses that text-edge
fallback only when estimated motion is finite, longer than 0.5 pixel, the current
pixel changed by at least 0.025, and the previous-frame color reprojected using
that vector matches within 0.03 maximum RGB error (normalized values).

Known port overlays still use the existing explicit preservation path. Reset,
static-pixel, invalid-output, stationary and out-of-range safeguards remain.
Motion estimation, depth filtering, camera metadata and sharpening are unchanged.
The motion texture is read through an additional SRV; the descriptor layout is
adjusted from 13 to 14 entries. There are no new image allocations, GPU copies or
presentation waits. The rejected periodic WaitForPresents/double-buffer experiment
is **not included**. This change does not resolve the separate async resource
lifetime concern. Moving subtitles and low-confidence scene motion still need
real-world evaluation.

## Validation and limits

- EU, US and JP executables included, based on the v1.1.4 distribution.
- Seven direct GPU shader cases passed: bright/dark moving shapes, static text,
  newly appearing text, overlays, reset and untrusted motion; zero D3D12 errors.
- SDK integration passed at 640x360 and 1920x1080, including exact static/HUD
  capture checks, scene reset, resize, disable/re-enable and presentation counts;
  zero D3D12 errors.
- The maintainer tested gameplay on AMD and reported no visible regression or
  perceptible performance loss in the tested scene. This is not a controlled
  performance benchmark. The NVIDIA outline fix remains unverified.
- Existing v1.1.4 fixes and film-grain setting are retained. No WARP/CPU rendering,
  private performance experiments or heavy diagnostic collector is included.

## Português

Pré-release experimental baseada na v1.1.4 para avaliar contornos claros com FSR
Frame Generation. Extraia em uma pasta separada, abra `Code Veronica.exe`, configure
os arquivos do seu jogo e ative 60 FPS + Frame Generation. Compare no mesmo cenário,
com a mesma resolução e nitidez. Confira personagem em movimento, legendas e overlays.

Informe se melhorou, ficou igual ou piorou, e se o FPS real mudou. Se possível,
envie um link de vídeo, GPU/driver e `bin/run-log.txt` após fechar o jogo.
O launcher ainda mostra v1.1.4; o marcador `edge-test1` no log identifica este teste.
O teste pesado de sincronização não está incluído. A build estável não foi alterada.
O Veronica Extractor 1.0.1 está disponível como download separado.


## Test 2: lightweight support report

The FSR shader is identical to Test 1. This revision adds one-time startup facts
to the existing `bin/run-log.txt`: Windows version, CPU model/instruction support
(including AVX2 OS readiness), RAM, selected GPU, DXGI driver version when available,
D3D feature/shader-model/wave-operation/format capabilities, and a whitelist of
effective rendering settings after the launcher applies them.

On an unhandled terminal exception, it attempts to record the exception code,
thread, registers and module-relative stack addresses. This does not catch every
type of failure and does not identify a visual outline automatically. It writes
no game-memory dump. No periodic sampler, background collector, GPU readback,
pixel counters or per-draw/per-frame logging is enabled by this report.

The expected steady-gameplay cost of the new startup-only queries is small;
performance of Test 2 has not been benchmarked in gameplay. Validation covers
startup/AVX OS-state checks, a synthetic terminal exception, hardware/WARP GPU
capability queries, and marker checks in all three regional executables. WARP is
used only by the standalone capability test, never selected by the shipped game.
Normal game/configuration logging may still contain local paths; review the log
before posting it publicly.

**Português:** a Test 2 mantém exatamente o shader da Test 1 e acrescenta apenas
o relatório leve de suporte na inicialização e tentativa de registro de falhas
terminais. Não inclui o coletor pesado. Envie `bin/run-log.txt` depois de fechar
o jogo, junto com as imagens do contorno. O marcador `[SUPPORT-BUILD]` identifica
a Test 2; `edge-test1` continua identificando a revisão do shader de bordas.
