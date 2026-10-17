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
set "GAME_ELF=%PS3_VFS_ROOT%\USRDIR\EBOOT.elf"
set "GAME_LOG=%~dp0run-log-release.txt"
rem The normal entry point always preserves FXC. DXC is opt-in per process;
rem it never becomes a saved launcher setting or leaks into the next launch.
set "SHADER_COMPILER=fxc"
if /i "%~1"=="--dxc-experimental" (
 set "SHADER_COMPILER=dxc"
 set "GAME_LOG=%~dp0run-log-dxc-experimental.txt"
 title Code Veronica - DXC experimental
 echo DXC EXPERIMENTAL: pode apresentar falhas nas sombras.
 echo Para voltar ao FXC, feche o jogo e use o iniciador normal.
)
rem Limpar opcoes de diagnostico que podem alterar o desenho.
for /f "delims==" %%V in ('set FP_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set TEX_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_EF_ 2^>nul') do set "%%V="
for /f "delims==" %%V in ('set SPURS_TASK_ 2^>nul') do set "%%V="
for %%V in (SCREENTEX CV_GPU_STATS CV_COMPOSITE_PROBE CV_VIDEO_CAPTURE RSX_CAP RSX_CAP_DIR VP_DUMP VP_BYPASS VP_W_ONE CULL_OFF DEPTH_OFF GCM_SUBCH_GATE) do set "%%V="
rem Explicit water diagnostic: apply AFTER clearing inherited debug options.
if "%CV_WATER_PROBE%"=="1" (
 set "FP_SHOW_CODE=AEBCCCCE"
 set "FP_SHOW=float4(saturate(r[4].y * 0.01), saturate(input.tc2.w), rsx_tex[2].Sample(rsx_samp[2], input.position.xy / float2(1280.0, 720.0)).x, 1.0)"
 set "RSX_CAP_DIR=rsxcap-water-alpha"
 set "RSXCAP_STEP_RANGE=1700,1900,4"
)
rem Water probe 2: thirds of the water show t3 (reflection), t0 (normal map)
rem by screen position, and frac(tc4.xz/20) (world position).
if "%CV_WATER_PROBE%"=="2" set "FP_SHOW_CODE=AEBCCCCE"
if "%CV_WATER_PROBE%"=="2" set "FP_SHOW=(input.position.x < 427.0 ? rsx_tex[3].Sample(rsx_samp[3], input.position.xy / float2(427.0, 720.0)) : (input.position.x < 853.0 ? rsx_tex[0].Sample(rsx_samp[0], input.position.xy / float2(427.0, 720.0)) : float4(frac(input.tc4.xz * 0.05), 0.0, 1.0)))"
if "%CV_WATER_PROBE%"=="2" set "RSX_CAP_DIR=rsxcap-water-probe2"
if "%CV_WATER_PROBE%"=="2" set "RSXCAP_STEP_RANGE=2220,2235,1"
if "%CV_WATER_PROBE%"=="2" set "TEX_WATCH=1"
rem Probe 3: probe 2 with the depth test off everywhere (the scene breaks):
rem shows how far the water mesh reaches when no depth test can hide it.
if "%CV_WATER_PROBE%"=="3" set "FP_SHOW_CODE=AEBCCCCE"
if "%CV_WATER_PROBE%"=="3" set "FP_SHOW=float4(frac(input.tc4.xz * 0.05), 0.0, 1.0)"
if "%CV_WATER_PROBE%"=="3" set "DEPTH_OFF=1"
if "%CV_WATER_PROBE%"=="3" set "RSX_CAP_DIR=rsxcap-water-probe3"
rem ...and F12 also saves every draw's vertex buffer and constants (op*_vb/cb.bin).
if "%CV_WATER_PROBE%"=="3" set "CV_VIDEO_CAPTURE=1"
rem Probe 4: the water's final alpha (red), its thickness /100 (green) and
rem the VS's tc2.w (blue), opaque. Black = transparent water.
if "%CV_WATER_PROBE%"=="4" set "FP_SHOW_CODE=AEBCCCCE"
if "%CV_WATER_PROBE%"=="4" set "FP_SHOW=float4(saturate(r[4].x), saturate(r[4].y * 0.01), saturate(input.tc2.w), 1.0)"
if "%CV_WATER_PROBE%"=="4" set "RSX_CAP_DIR=rsxcap-water-probe4"
if "%CV_WATER_PROBE%"=="4" set "CV_VIDEO_CAPTURE=1"
rem Probe 5: normal picture; saves the water's 256x256 reflection texture
rem bytes (texdump_*.bin) each time it is uploaded.
if "%CV_WATER_PROBE%"=="5" set "TEX_DUMP=07783780"
if "%CV_WATER_PROBE%"=="5" set "TEX_WATCH=1"
rem Probe 6: normal picture, the water's patched constants (normal-map UVs
rem c0/c1/c3/c4, depth unprojection c5-c8) zeroed as RPCS3 has them. F12 saves to rsxcap-water-probe6.
if "%CV_WATER_PROBE%"=="6" set "FP_ZERO_K=AEBCCCCE:0,1,3,4,5,6,7,8"
if "%CV_WATER_PROBE%"=="6" set "RSX_CAP_DIR=rsxcap-water-probe6"
rem Probe 8: normal picture; F12 also saves the scene right before and right
rem after the water draw (its 138612-vertex mesh) into rsxcap-water-probe8.
if "%CV_WATER_PROBE%"=="8" set "RSXCAP_STEP_VTX=138612"
if "%CV_WATER_PROBE%"=="8" set "RSX_CAP_DIR=rsxcap-water-probe8"
rem Probe 9: the water's normal map (units 0, 1) read raw, without the sRGB
rem decode, as RPCS3 reads it. Probe 10: the reflection cube (unit 3) too.
if "%CV_WATER_PROBE%"=="9" set "TEX_NOGAMMA_FP=AEBCCCCE:0,1"
if "%CV_WATER_PROBE%"=="10" set "TEX_NOGAMMA_FP=AEBCCCCE:0,1,3"
rem Mip levels test: upload the textures' mip chains (RSX_TEX_MIPS).
if "%CV_TEX_MIPS%"=="1" set "RSX_TEX_MIPS=1"
if "%CV_TEX_MIPS%"=="1" set "RSX_CAP_DIR=rsxcap-mips"
if "%CV_TEX_MIPS%"=="1" set "RSXCAP_STEP_VTX=138612"
rem Cube orientation tests: CV_CUBE_FLIP=x / y / z mirrors the reflection
rem lookup on that axis. F12 saves to rsxcap-cubo-<axis>.
if not "%CV_CUBE_FLIP%"=="" set "RSX_CUBE_FLIP=%CV_CUBE_FLIP%"
if not "%CV_CUBE_FLIP%"=="" set "RSX_CAP_DIR=rsxcap-cubo-%CV_CUBE_FLIP%"
if not "%CV_CUBE_FLIP%"=="" set "RSXCAP_STEP_VTX=138612"
rem Probe 7, thirds of the water: the reflection cube along a screen-made
rem direction / reflected off a flat surface / the shader's real colour x8.
if "%CV_WATER_PROBE%"=="7" set "FP_SHOW_CODE=AEBCCCCE"
if "%CV_WATER_PROBE%"=="7" set "FP_SHOW=(input.position.x < 427.0 ? rsx_tex3.Sample(rsx_samp[3], float3(input.position.x / 213.0 - 1.0, 1.0 - input.position.y / 360.0, 1.0)) * 3.0 : (input.position.x < 853.0 ? rsx_tex3.Sample(rsx_samp[3], reflect(normalize(input.tc4.xyz - fp_k[9].xyz), normalize(input.tc1.xyz))) * 3.0 : h[0] * 8.0))"
if "%CV_WATER_PROBE%"=="7" set "RSX_CAP_DIR=rsxcap-water-probe7"
if "%CV_WATER_PROBE%"=="7" set "TEX_DUMP=07783780"
rem Water transfer trace: file gate is controlled during the affected room.
if "%CV_WATER_TRANSFERS%"=="1" set "NV3089_GATE=%~dp0trace-agua.enable"
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
if "%CV_RENDERDOC%"=="1" (
 if not exist "D:\Tools\capturas\nosso" mkdir "D:\Tools\capturas\nosso"
 "D:\Tools\RenderDoc_1.46_64\renderdoccmd.exe" capture -w --opt-hook-children -d "%~dp0." -c "D:\Tools\capturas\nosso\agua" "%GAME_EXE%" "%GAME_ELF%" > "%GAME_LOG%" 2>&1
) else (
 "%GAME_EXE%" "%GAME_ELF%" > "%GAME_LOG%" 2>&1
)
set "GAME_RESULT=%ERRORLEVEL%"
echo [runner] exit code=%GAME_RESULT%>> "%GAME_LOG%"
echo.
echo O jogo foi encerrado. Codigo de saida: %GAME_RESULT%
echo Se fechou sem abrir a janela, envie o arquivo run-log-release.txt.
:finished
echo.
pause
endlocal
