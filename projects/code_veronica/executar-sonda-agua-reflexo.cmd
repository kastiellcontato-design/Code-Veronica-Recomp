@echo off
setlocal
rem Water probe 5: normal picture; saves the bytes of the water's reflection
rem texture (texdump_*.bin) each time the game uploads it.
set "CV_WATER_PROBE=5"
echo Sonda do reflexo da agua: a imagem fica normal.
echo Va na sala da agua, espere uns segundos e aperte F12.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
