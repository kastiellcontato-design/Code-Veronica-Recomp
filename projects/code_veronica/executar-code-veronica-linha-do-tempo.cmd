@echo off
setlocal
rem Measurement only: PS3_TIMELINE writes, every 5 s, how many milliseconds
rem per frame each stage and each wait of the frame pipeline took ([TL] in
rem run-log-release.txt). Process-only; the normal shortcut stays as it was.
set "PS3_TIMELINE=1"
echo MEDICAO: linha do tempo por quadro ([TL] no log).
echo Va ate a sala pesada e fique parado uns 20 segundos.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal
