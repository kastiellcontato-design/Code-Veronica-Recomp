@echo off
setlocal
rem Diagnostic only: reuse existing hitch counters and timeline.
set "HITCHDBG=1"
set "PERF=1"
set "PS3_TIMELINE=1"
echo DIAGNOSTICO DE TIROS: aguarde a preparacao e carregue uma partida.
echo Fique parado 10 segundos, dispare varias vezes e aguarde mais 10 segundos.
echo Nao precisa apertar F12. Avise no chat quando terminar.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal