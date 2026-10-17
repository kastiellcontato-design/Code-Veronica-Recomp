@echo off
setlocal
rem Water probe 7: the water in three test strips (reflection cube, flat
rem reflection, real colour brightened). F12 saves to rsxcap-water-probe7.
set "CV_WATER_PROBE=7"
echo Sonda do reflexo da agua: a agua vai aparecer em tres faixas de teste, e normal.
echo Va na sala da agua e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
