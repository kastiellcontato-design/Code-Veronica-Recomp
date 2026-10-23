@echo off
setlocal
cd /d "%~dp0"
if /i "%~1"=="--on" goto on
if /i "%~1"=="--off" goto off
echo.
echo CODE VERONICA - TESTE 30 FPS - JAPONES / DXC
echo 1. Ligar otimizacoes
echo 2. Desligar otimizacoes para comparar
echo 3. Sair
choice /c 123 /n /m "Escolha: "
if errorlevel 3 exit /b 0
if errorlevel 2 goto off
:on
set "CV30_OPTIMIZATIONS=1"
set "GAME_LOG=%~dp0run-log-30fps-otimizacoes-on.txt"
echo Otimizacoes LIGADAS.
goto launch
:off
set "CV30_OPTIMIZATIONS=0"
set "GAME_LOG=%~dp0run-log-30fps-otimizacoes-off.txt"
echo Otimizacoes DESLIGADAS neste pacote de comparacao.
:launch
set "CV_FPS_TEST_60="
set "CV_FPS_BENCHMARK_RAW="
set "CV_RENDER_INTERPOLATION="
set "CV_POSE_PROFILE="
set "CV_FRAME_PROFILE="
set "CV_FRAME_DETAIL="
set "CV_PREP_NOREUSE="
set "CV_SHADOW_BATCH="
set "RSX_GPU_VFETCH=1"
set "CV_NO_LAUNCHER="
set "CV_LAUNCHER_AUTOPLAY="
set "SHADER_COMPILER=dxc"
set "VBLANK_HZ=60"
set "CV_AV_CONTINUOUS_PTS=1"
set "SPU_INTERP_UNLIFTED=1"
set "SPU_MAX_RUNNING=10"
set "PPU_BOOST_NAMES=snddrv"
set "PS3_VERBOSE=0"
set "SCREEN_AS_TEX=0"
set "VP_NOFIXPROJ=1"
set "GCM_PARK_GRACE_MS=5000"
set "PERF="
set "PS3_TIMELINE="
set "CV_LIGHTER_PROBE="
set "FP_ZERO_K="
set "GAME_EXE=%~dp0build-release\bin\CodeVeronicaPort_JP.exe"
if not exist "%GAME_EXE%" (
 echo Executavel japones nao encontrado: %GAME_EXE%
 pause
 exit /b 1
)
echo No launcher, use a pasta japonesa e mantenha a mesma resolucao para comparar.
echo O limite fica em 30 FPS neste teste. A escolha salva de FPS sera preservada.
echo Para trocar ON/OFF, feche o jogo e abra este iniciador novamente.
echo Log: %GAME_LOG%
if exist "%GAME_LOG%" copy /y "%GAME_LOG%" "%GAME_LOG%.previous.txt" >nul
>"%GAME_LOG%" echo [runner] native30=1 package=%CV30_OPTIMIZATIONS% compiler=dxc gpu_vertex_fetch=1
"%GAME_EXE%" >>"%GAME_LOG%" 2>&1
set "GAME_RESULT=%ERRORLEVEL%"
echo [runner] exit code=%GAME_RESULT%>>"%GAME_LOG%"
echo Jogo encerrado. Codigo: %GAME_RESULT%
pause
exit /b %GAME_RESULT%
