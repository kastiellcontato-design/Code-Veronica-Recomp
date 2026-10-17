@echo off
setlocal
rem Water probe 4: paints the water with its transparency (red), thickness
rem (green) and tc2.w (blue). F12 saves to rsxcap-water-probe4.
set "CV_WATER_PROBE=4"
echo Sonda da transparencia da agua: a agua vai aparecer colorida, e normal.
echo Va na sala da agua e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
