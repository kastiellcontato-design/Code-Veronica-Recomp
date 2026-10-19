@echo off
setlocal
set "CV_FPS_DIAGNOSTIC=1"
rem First cadence experiment, NOT a verified gameplay-speed fix.
rem No saved settings, executable patches, or persistent clock changes.
set "CV_FPS_TEST_60=1"
set "CV_RENDER_INTERPOLATION="
set "CV_FPS_BENCHMARK_RAW="
set "CV_WATER_PROBE="
set "CV_WATER_TRANSFERS="
set "SRT_LOG=1"
echo.
echo Code Veronica - experimento: logica 30 Hz, desenho ate 60 FPS
echo Compare 30 segundos reais com o cronometro do SRT durante a partida.
echo Se o jogo acelerar ou o audio perder sincronia, encerre este teste.
echo O launcher ainda mostra o limite original; confira os FPS dentro do jogo.
echo Log separado: run-log-fps60-experimental.txt
 echo.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
