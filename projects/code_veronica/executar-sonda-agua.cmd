@echo off
setlocal
rem Water shader probe: the water shows test colours instead of its normal
rem look (left: reflection texture, middle: normal map, right: world position).
rem Only for diagnosis; F12 saves to rsxcap-water-probe2.
set "CV_WATER_PROBE=2"
rem [TEXWATCH]: the water textures' source bytes when uploaded.
set "TEX_WATCH=1"
rem [VTXWATCH]: format and position range of the water mesh.
set "VTX_WATCH=1"
echo Sonda da agua: a agua vai aparecer com cores de teste. Aperte F12 na sala.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
