@echo off
setlocal
set "CV_FPS_DIAGNOSTIC=1"
set "CV_FPS_TEST_60=1"
set "CV_FPS_BENCHMARK_RAW="
set "CV_RENDER_INTERPOLATION=1"
set "CV_POSE_CPU="
set "CV_POSE_CPU_BLEND=1"
if /i "%~1"=="--gpu-blend" set "CV_POSE_CPU_BLEND=0"
if /i "%~1"=="--cpu-blend" set "CV_POSE_CPU_BLEND=1"
if /i "%~1"=="--cpu" set "CV_POSE_CPU=1"
rem Lightweight stage timings: one aggregate line per 5 s, no per-draw probes.
set "CV_POSE_PROFILE=1"
rem Coarse frame timings + sampled GPU lists; no heavy draw-call hooks.
set "CV_FRAME_PROFILE=1"
if /i "%~1"=="--no-frame-profile" set "CV_FRAME_PROFILE="
set "CV_WATER_PROBE="
set "CV_WATER_TRANSFERS="
rem These probes add hitches and must not be used to judge smoothness.
set "PERF="
if /i "%~1"=="--prepass-detail" set "PERF=1"
set "PS3_TIMELINE="
set "FRAME_BUDGET="
echo TESTE DE INTERPOLACAO: logica 30 Hz, poses intermediarias no desenho.
if "%CV_POSE_CPU%"=="1" echo Comparacao: conversao de vertices na CPU (caminho anterior).
if /i "%~1"=="--prepass-detail" echo Diagnostico PERF ativo: as medidas de tempo podem ter algum custo.
if "%CV_POSE_CPU_BLEND%"=="1" echo Comparacao: calculo final das posicoes na CPU.
echo Teste caminhada, giro e troca de camera. Para voltar, use o atalho habitual.
echo Log separado: run-log-fps60-interpolado.txt
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal
