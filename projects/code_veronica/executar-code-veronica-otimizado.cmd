@echo off
setlocal
cd /d "%~dp0"
title Code Veronica - iniciador (otimizado)
set "PS3_VFS_ROOT=D:\ps3recomp\JOGO\NPEB00553"
set "PS3_DEV_FLASH=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_flash"
set "PS3_RAP_DIR=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_hdd0\home\00000001\exdata"
set "SPU_INTERP_UNLIFTED=1"
rem Limite de tarefas pesadas de SPU rodando ao mesmo tempo.
set "SPU_MAX_RUNNING=10"
rem Threads de som do jogo com prioridade maior (evita audio picotando).
set "PPU_BOOST_NAMES=snddrv"
rem Resolucao: 1.5 = 1080p (1920x1080). Para voltar ao 720p, deixe o rem na frente.
rem set "RES_SCALE=1.5"
rem Diagnosticos de audio (desligados).
rem set "AUDIO_RATE=1"
rem set "AUDIO_WRITER=2"
rem set "AUDIO_PEAK=1"
rem set "AUDIO_WAV=%~dp0audio-capture.f32"
set "CV_AV_CONTINUOUS_PTS=1"
set "PS3_VERBOSE=0"
set "SCREEN_AS_TEX=0"
rem A projecao do jogo e valida mesmo com c0.x fora de (0,8); nao trocar.
set "VP_NOFIXPROJ=1"
rem O jogo deixa a FIFO "estacionada" em 0x198060 e a libera depois; nao pular.
set "GCM_PARK_GRACE_MS=5000"
rem --- Diagnosticos: DESLIGADOS (pesam e causam trancos). Para ligar um,
rem --- tire o 'rem ' da frente da linha.
rem Captura automatica (rsxcap) de um quadro da gameplay:
rem set "CV_AUTOCAP=1300"
rem Tempo por etapa a cada 20 quadros ([PERF] no log):
rem set "PERF=1"
rem Linha do tempo por quadro a cada 5 s ([TL] no log):
rem set "PS3_TIMELINE=1"
rem Tarefas de SPU por imagem a cada 5 s ([SPU-STAT] no log):
rem set "PS3_SPU_STAT=1"
rem Sequencia de eventos de troca de quadro apos N segundos ([FLIPTRACE]):
rem set "PS3_FLIPTRACE=40"
rem Amostrador de CPU ([samp] no log):
rem set "PS3_SAMPLE=2"
rem set "PS3_SAMPLE_SPU=1"
rem Onde o jogo chama usleep, a cada 10 s:
rem set "PS3_POLLTOP=10"
set "GAME_EXE=%~dp0build-release\bin\MyGameRecomp.exe"
if "%CV_POSE_COMPUTE_TEST%"=="1" set "GAME_EXE=%~dp0build-release\bin\MyGameRecomp_JP.exe"
if "%CV_FSR_LAUNCHER_JP%"=="1" set "GAME_EXE=%~dp0build-release\bin\MyGameRecomp_JP.exe"
set "GAME_ELF=%PS3_VFS_ROOT%\USRDIR\EBOOT.elf"
set "GAME_LOG=%~dp0run-log-release.txt"
rem DXC is the normal shader compiler (tested better than FXC). FXC stays
rem available per process with --fxc; neither is saved in the launcher.
set "SHADER_COMPILER=dxc"
if /i "%~1"=="--fxc" set "SHADER_COMPILER=fxc"
if /i "%~1"=="--dxc-experimental" (
 set "SHADER_COMPILER=dxc"
 set "GAME_LOG=%~dp0run-log-dxc-experimental.txt"
 title Code Veronica - DXC experimental
 echo DXC EXPERIMENTAL: pode apresentar falhas nas sombras.
 echo Para usar o FXC, feche o jogo e rode este atalho com --fxc.
)
rem Always restore the native clock on normal entry points. The experiment
rem is process-local, never saved to cv_launcher.ini or written into the ELF.
set "VBLANK_HZ=60"
if "%CV_FPS_TEST_60%"=="1" (
 set "VBLANK_HZ=60"
 set "GAME_LOG=%~dp0run-log-fps60-experimental.txt"
 title Code Veronica - teste de 60 FPS
 echo TESTE: desenho em 60 FPS; logica do jogo em 30 Hz; vblank em 60 Hz.
 if "%CV_FPS_BENCHMARK_RAW%"=="1" echo BENCHMARK RAW: este modo acelera a logica do jogo.
 if "%CV_RENDER_INTERPOLATION%"=="1" echo Interpolacao experimental de geometria ativada. Confira movimento e cortes de camera.
 echo Para voltar aos 30 FPS, feche e use o atalho normal.
)
if "%CV_FPS_TEST_60%"=="1" if "%CV_FPS_BENCHMARK_RAW%"=="1" set "GAME_LOG=%~dp0run-log-benchmark60-acelerado.txt"
if not "%CV_FPS_TEST_60%"=="1" set "CV_RENDER_INTERPOLATION="
if "%CV_FPS_TEST_60%"=="1" if "%CV_RENDER_INTERPOLATION%"=="1" set "GAME_LOG=%~dp0run-log-fps60-interpolado.txt"
if "%CV_POSE_COMPUTE_TEST%"=="1" if "%CV_POSE_COMPUTE%"=="1" set "GAME_LOG=%~dp0run-log-fps60-gpu-compute.txt"
if "%CV_POSE_COMPUTE_TEST%"=="1" if "%CV_POSE_COMPUTE%"=="0" set "GAME_LOG=%~dp0run-log-fps60-cpu-referencia.txt"
if "%CV_LIGHTER_PROBE%"=="1" set "GAME_LOG=%~dp0run-log-sombra-isqueiro.txt"
rem Limpar opcoes de diagnostico que podem alterar o desenho.
for /f "delims==" %%V in ('set FP_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set TEX_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_EF_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_TASK_ 2^>nul') do set "%%V="
for %%V in (SCREENTEX CV_GPU_STATS CV_COMPOSITE_PROBE CV_VIDEO_CAPTURE RSX_CAP RSX_CAP_DIR VP_DUMP VP_BYPASS VP_W_ONE CULL_OFF DEPTH_OFF GCM_SUBCH_GATE) do set "%%V="
rem Estatistica de espera de event flag (desligada; fica depois da limpeza acima).
rem set "SPURS_EF_STATS=1"
if not exist "%GAME_EXE%" (
 echo ERRO: executavel nao encontrado:
 echo %GAME_EXE%
 goto finished
)
if not exist "%GAME_ELF%" (
 echo ERRO: arquivo do jogo nao encontrado:
 echo %GAME_ELF%
 goto finished
)
for %%D in (avcodec-61.dll avutil-59.dll swscale-8.dll swresample-5.dll) do if not exist "%~dp0build-release\bin\%%D" (
 echo ERRO: biblioteca %%D ausente na pasta do executavel.
 goto finished
)
echo Abrindo Code Veronica. Aguarde a janela do jogo.
echo Esta versao ainda tem falhas de imagem.
echo O log desta execucao sera salvo em:
echo %GAME_LOG%
echo Para encerrar, feche a janela do jogo ou pressione Ctrl+C aqui.
echo.
rem Preserve the last three runs: a restart must not erase the freeze evidence.
if exist "%GAME_LOG%.previous-2.txt" copy /y "%GAME_LOG%.previous-2.txt" "%GAME_LOG%.previous-3.txt" >nul
if exist "%GAME_LOG%.previous-1.txt" copy /y "%GAME_LOG%.previous-1.txt" "%GAME_LOG%.previous-2.txt" >nul
if exist "%GAME_LOG%" copy /y "%GAME_LOG%" "%GAME_LOG%.previous-1.txt" >nul
> "%GAME_LOG%" echo [runner] vblank_hz=%VBLANK_HZ% fps_test_60=%CV_FPS_TEST_60% shader_compiler=%SHADER_COMPILER%
"%GAME_EXE%" "%GAME_ELF%" >> "%GAME_LOG%" 2>&1
set "GAME_RESULT=%ERRORLEVEL%"
echo [runner] exit code=%GAME_RESULT%>> "%GAME_LOG%"
echo.
echo O jogo foi encerrado. Codigo de saida: %GAME_RESULT%
echo Se fechou sem abrir a janela, envie o arquivo run-log-release.txt.
:finished
echo.
pause
endlocal
