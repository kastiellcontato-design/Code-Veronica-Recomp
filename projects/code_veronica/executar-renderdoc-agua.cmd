@echo off
setlocal
rem Runs the game under RenderDoc with debuggable shaders, to capture the
rem flooded room's water frame. Captures go to D:\Tools\capturas\nosso.
set "CV_RENDERDOC=1"
set "RSX_DXC_DEBUG=1"
echo Jogo com o RenderDoc: va na sala da agua e aperte Print Screen (PrtScn) UMA vez.
echo Depois feche o jogo normalmente.
call "%~dp0executar-code-veronica-dxc-experimental.cmd"
dir /b "D:\Tools\capturas\nosso"
endlocal
