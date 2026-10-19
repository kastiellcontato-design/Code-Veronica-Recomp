@echo off
setlocal
set "CV_FPS_DIAGNOSTIC=1"
rem Preserve the unlocked, accelerated benchmark for the user's comparisons.
set "CV_FPS_TEST_60=1"
set "CV_RENDER_INTERPOLATION="
set "CV_FPS_BENCHMARK_RAW=1"
set "CV_WATER_PROBE="
set "CV_WATER_TRANSFERS="
echo BENCHMARK ACELERADO: a logica acompanha os FPS. Nao e o modo de velocidade normal.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
