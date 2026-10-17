@echo off
setlocal
rem Water probe 8: normal picture; F12 also saves the scene right before and
rem right after the water draw. Saves to rsxcap-water-probe8.
set "CV_WATER_PROBE=8"
echo Sonda da agua: imagem normal. Va na sala da agua e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
