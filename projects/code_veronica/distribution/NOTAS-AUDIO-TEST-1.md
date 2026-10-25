## Audio compatibility test 1 — based on v1.1

**Test build for issue #5. This is not a stable update and will not be offered by the launcher's automatic updater. Stable v1.1 remains available.**

The supplied logs report `WASAPI Initialize failed: 0x88890008` (`AUDCLNT_E_UNSUPPORTED_FORMAT`). This build keeps the existing stereo 48 kHz float output when the device accepts it. If that format is rejected, it retries with Windows shared-mode format conversion. Failed initialization attempts use a fresh audio client, and the log records the selected path.

### How to test
1. Close the game and launcher. Keep the stable v1.1 ZIP for rollback.
2. Extract **Code.Veronica.Audio.Format.Test.1.zip** over your existing port installation folder. The package contains no saves, settings, game files, or caches.
3. Start **Code Veronica.exe**. Test the **same output device that had no sound**. You do not need to change its sample rate or disable SteelSeries GG / NVIDIA Broadcast for this test.
4. Close the game and attach **bin/run-log.txt** to [issue #5](https://github.com/kastieloficial/RE-Code-Veronica-Recomp/issues/5), mentioning whether audio returned and which output device you used. The log should identify **AUDIO FORMAT TEST 1**.
5. To roll back, close the game and extract the stable [v1.1 ZIP](https://github.com/kastieloficial/RE-Code-Veronica-Recomp/releases/tag/v1.1) over the installation again.

EU, US and JP executables are included. The v1.1 shadow fixes are retained.

### Validation
- Nine mocked WASAPI initialization scenarios passed, covering native output, rejected PCM/sample-rate and multichannel formats, conversion, polling fallback, and error cleanup.
- The actual local Windows endpoint initialized successfully through the unchanged native-format path; no samples were played during that probe.
- All three regional GUI builds and shared-log startup/handoff tests passed.
- **Audio playback on the affected users' devices still needs confirmation.** No performance improvement is claimed.

## Português

**Build de teste para o problema de áudio da issue #5. Não substitui a v1.1 estável e não aparece como atualização automática no launcher.**

Feche o jogo e extraia o ZIP completo sobre a instalação existente. Abra **Code Veronica.exe** e use a mesma saída que estava sem som. Depois, feche o jogo e envie **bin/run-log.txt** na issue #5, informando se o áudio voltou e qual dispositivo utilizou.

O pacote mantém as correções de sombras da v1.1. Para voltar, feche o jogo e extraia novamente o ZIP estável da v1.1. A confirmação nos dispositivos afetados é necessária antes de promover a correção a uma versão estável.
