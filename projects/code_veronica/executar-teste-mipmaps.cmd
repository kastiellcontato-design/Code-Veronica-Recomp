@echo off
setlocal
rem Test: textures with their mip levels (RSX_TEX_MIPS=1). Picture otherwise
rem normal. F12 saves to rsxcap-mips.
set "CV_TEX_MIPS=1"
echo Teste: texturas com mipmaps. Va na sala da agua e aperte F12.
echo Repare tambem em outras salas: texturas de longe devem ficar mais suaves.
call "%~dp0executar-code-veronica-desempenho.cmd"
endlocal
