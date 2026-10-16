@echo off
rem Optional test only. The normal optimized launcher continues to use FXC.
setlocal
call "%~dp0executar-code-veronica-otimizado.cmd" --dxc-experimental
endlocal
