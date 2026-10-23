@echo off
setlocal
cd /d "%~dp0"
title Code Veronica - aparencia da Claire (JP)
for %%V in (CV_FPS_DIAGNOSTIC CV_FPS_TEST_60 CV_FPS_BENCHMARK_RAW CV_RENDER_INTERPOLATION CV_POSE_PROFILE CV_POSE_SEARCH_PROFILE CV_FRAME_PROFILE CV_FRAME_DETAIL CV_WORK_PROFILE CV_MESH_ID_DISCOVERY CV_POSE_COMPUTE_TEST CV_AUTOCAP PERF PS3_SAMPLE PS3_SAMPLE_SPU) do set "%%V="
set "CV_NO_LAUNCHER="
set "CV_NATIVE_PC_MENU=1"
set "PS3_VERBOSE=0"
set "SPU_INTERP_UNLIFTED=1"
set "SPU_MAX_RUNNING=10"
set "PPU_BOOST_NAMES=snddrv"
set "CV_AV_CONTINUOUS_PTS=1"
set "VP_NOFIXPROJ=1"
set "GCM_PARK_GRACE_MS=5000"
set "SHADER_COMPILER=dxc"
set "VBLANK_HZ=60"
set "APPEARANCE_EXE=%~dp0build-release\bin\CodeVeronicaPort_JP_Appearance.exe"
if not exist "%APPEARANCE_EXE%" (
 echo Executavel de aparencia nao encontrado. Rode build-appearance-jp.cmd.
 pause
 exit /b 1
)
echo Teste da versao japonesa. Escolha a pasta NPJB00135 no launcher.
echo No jogo: OPTION - RECOMP SETTINGS - APARENCIA.
echo Esquerda/direita trocam a aparencia; confirmar tenta aplicar novamente.
echo A opcao ORIGINAL restaura a aparencia normal da Claire.
"%APPEARANCE_EXE%" > "%~dp0run-log-aparencia-jp.txt" 2>&1
set "APPEARANCE_RESULT=%ERRORLEVEL%"
echo O jogo foi encerrado. Codigo: %APPEARANCE_RESULT%
echo Log: %~dp0run-log-aparencia-jp.txt
pause
exit /b %APPEARANCE_RESULT%
