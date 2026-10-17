@echo off
setlocal
rem Water probe 3: the whole water surface in test colours with the depth
rem test off everywhere. The rest of the scene WILL look broken; only for
rem seeing how far the water reaches. F12 saves to rsxcap-water-probe3.
set "CV_WATER_PROBE=3"
echo Sonda da agua SEM profundidade: a cena vai ficar quebrada, e normal.
echo Va na sala da agua e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
