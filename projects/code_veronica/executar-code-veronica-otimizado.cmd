@echo off
setlocal
cd /d "%~dp0"
title Code Veronica - iniciador (otimizado)
set "PS3_VFS_ROOT=D:\ps3recomp\JOGO\NPEB00553"
set "PS3_DEV_FLASH=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_flash"
set "PS3_RAP_DIR=D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_hdd0\home\00000001\exdata"
set "SPU_INTERP_UNLIFTED=1"
set "CV_AV_CONTINUOUS_PTS=1"
set "PS3_VERBOSE=0"
set "SCREEN_AS_TEX=0"
rem A projecao do jogo e valida mesmo com c0.x fora de (0,8); nao trocar.
set "VP_NOFIXPROJ=1"
rem O jogo deixa a FIFO "estacionada" em 0x198060 e a libera depois; nao pular.
set "GCM_PARK_GRACE_MS=5000"
rem Diagnostico: captura automatica (rsxcap) de um quadro da gameplay.
set "CV_AUTOCAP=1300"
rem Diagnostico: tempo por etapa a cada 20 quadros ([PERF] no log).
set "PERF=1"
rem Diagnostico: amostra a thread principal a cada 2 ms ([samp] no log).
rem set "PS3_SAMPLE=2"
rem set "PS3_SAMPLE_MAIN=1"
rem set "PS3_SAMPLE_THREAD=frame_clock"
rem Diagnostico: onde o jogo chama usleep, a cada 10 s.
set "PS3_POLLTOP=10"
set "GAME_EXE=%~dp0build-release\bin\MyGameRecomp.exe"
set "GAME_ELF=%PS3_VFS_ROOT%\USRDIR\EBOOT.elf"
set "GAME_LOG=%~dp0run-log-release.txt"
rem Limpar opcoes de diagnostico que podem alterar o desenho.
for /f "delims==" %%V in ('set FP_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set TEX_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_EF_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_TASK_ 2^>nul') do set "%%V="
for %%V in (SCREENTEX CV_GPU_STATS CV_COMPOSITE_PROBE CV_VIDEO_CAPTURE RSX_CAP RSX_CAP_DIR VP_DUMP VP_BYPASS VP_W_ONE CULL_OFF DEPTH_OFF GCM_SUBCH_GATE) do set "%%V="
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
"%GAME_EXE%" "%GAME_ELF%" > "%GAME_LOG%" 2>&1
set "GAME_RESULT=%ERRORLEVEL%"
echo.
echo O jogo foi encerrado. Codigo de saida: %GAME_RESULT%
echo Se fechou sem abrir a janela, envie o arquivo run-log-release.txt.
:finished
echo.
pause
endlocal
