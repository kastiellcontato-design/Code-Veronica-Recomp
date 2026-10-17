@echo off
setlocal
rem Water probe 6: the water without its ripple constants, as RPCS3 draws
rem it. F12 saves to rsxcap-water-probe6.
set "CV_WATER_PROBE=6"
echo Teste da agua sem ondulacao (como no RPCS3).
echo Va na sala da agua e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
