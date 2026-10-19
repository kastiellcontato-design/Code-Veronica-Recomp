@echo off

setlocal

cd /d "%~dp0"

rem Diagnostic for Steve's machine-gun cutscene, rm_2050 (area 2, room 05).

rem Existing per-hitch shader counters and stage timeline. Keep caches/options.

set "HITCHDBG=1"
set "CV_CUTSCENE_DIAG=1"

set "PS3_TIMELINE=1"

rem Avoid the per-draw timing overhead of PERF during the first measurement.

set "PERF=0"

echo.

echo DIAGNOSTICO DA CUTSCENE - STEVE - RM_2050

 echo Carregue o save e aguarde 10 segundos antes de iniciar a cena.

echo Deixe passar a parte dos disparos e aguarde mais 10 segundos.

echo Avise no chat quando terminar. Nao precisa apertar F12.

echo Log: %~dp0run-log-release.txt

echo.

call "%~dp0executar-code-veronica-otimizado.cmd"

endlocal
