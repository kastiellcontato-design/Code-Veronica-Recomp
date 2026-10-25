@echo off
setlocal
cd /d "%~dp0"
title Resident Evil Code Veronica X
rem Normal play: saved graphics/FPS/region choices belong to the launcher.
for %%V in (CV_FPS_DIAGNOSTIC CV_FPS_TEST_60 CV_FPS_BENCHMARK_RAW CV_FPS_UNLOCKED CV_RENDER_INTERPOLATION CV_POSE_CPU CV_POSE_CPU_BLEND CV_POSE_PROFILE CV_POSE_SEARCH_PROFILE CV_FRAME_PROFILE CV_FRAME_DETAIL CV_PRODUCER_PROFILE CV_FIFO_PROFILE CV_FIFO_NOTIFY CV_POSE_COMPUTE CV_POSE_COMPUTE_TEST CV_FSR_FRAME_GENERATION CV_FSR_DIAGNOSTIC CV_WORK_PROFILE CV_WORK_PROFILE_STACKS CV_MESH_DISCOVERY CV_SPU_DESCRIPTOR_TEST CV_SPU_NATIVE_DESCRIPTORS CV_SPU_NATIVE_DRAW CV_SPU_NATIVE_VERIFY CV_LIGHTER_PROBE CV30_OPTIMIZATIONS CV_NO_LAUNCHER CV_LAUNCHER_AUTOPLAY CV_FSR_LAUNCHER_JP CV_AUTOCAP CV_VIDEO_CAPTURE RSX_CAP RSX_CAP_DIR PERF PERF_THREADS PS3_TIMELINE FRAME_BUDGET PS3_SAMPLE PS3_SAMPLE_MAIN PS3_SAMPLE_THREAD PS3_SAMPLE_SPU PS3_POLLTOP SRT_LOG AUDIO_WAV AUDIO_RATE AUDIO_WRITER AUDIO_PEAK) do set "%%V="
for /f "delims==" %%V in ('set FP_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set TEX_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_EF_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_TASK_ 2^>nul') do set "%%V="
for %%V in (SCREENTEX CV_GPU_STATS CV_COMPOSITE_PROBE VP_DUMP VP_BYPASS VP_W_ONE CULL_OFF DEPTH_OFF GCM_SUBCH_GATE) do set "%%V="
set "SPU_MFC_STAGE_FAST=0"
set "PS3_VFS_ROOT=D:\ps3recomp\JOGO\NPEB00553"
set "PS3_DEV_FLASH=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_flash"
set "PS3_RAP_DIR=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_hdd0\home\00000001\exdata"
set "SPU_INTERP_UNLIFTED=1"
set "SPU_MAX_RUNNING=10"
set "PPU_BOOST_NAMES=snddrv"
if not defined SPU_TASK_POOL set "SPU_TASK_POOL=1"
set "CV_AV_CONTINUOUS_PTS=1"
set "PS3_VERBOSE=0"
set "SCREEN_AS_TEX=0"
set "VP_NOFIXPROJ=1"
set "GCM_PARK_GRACE_MS=5000"
set "SHADER_COMPILER=dxc"
set "VBLANK_HZ=60"
set "GAME_EXE=%~dp0build-release\bin\CodeVeronicaPort.exe"
for %%F in (CodeVeronicaPort.exe CodeVeronicaPort_JP.exe CodeVeronicaPort_US.exe avcodec-61.dll avutil-59.dll swscale-8.dll swresample-5.dll dxcompiler.dll dxil.dll) do if not exist "%~dp0build-release\bin\%%F" (
 echo Arquivo necessario ausente: %%F
 pause
 exit /b 1
)
if /i "%~1"=="--check" (
 echo OK: launcher e tres executaveis regionais presentes. Diagnosticos desligados.
 exit /b 0
)
rem Shared console prevents main.cpp from creating a run-log.txt on startup.
"%GAME_EXE%" >nul 2>&1
set "GAME_RESULT=%ERRORLEVEL%"
if not "%GAME_RESULT%"=="0" (
 echo O jogo encerrou com codigo %GAME_RESULT%.
 pause
)
exit /b %GAME_RESULT%
