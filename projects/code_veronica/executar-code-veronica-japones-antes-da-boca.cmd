@echo off
setlocal
rem Comparacao: interpolacao como antes da correcao da boca do Steve
rem (commit 597838d). Mantem a thread de pose; o conflito de historicos
rem volta a regra antiga, e a boca do Steve volta a rasgar neste atalho.
set "CV_POSE_CONFLICT_FOLLOW=0"
call "%~dp0executar-code-veronica-japones.cmd"
endlocal
