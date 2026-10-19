@echo off
setlocal
rem Test: the vertex shader converts the guest's vertex bytes on the GPU
rem (RSX_GPU_VFETCH). Process-only; the normal shortcut stays as it was.
set "RSX_GPU_VFETCH=1"
echo TESTE: conversao de vertices na GPU (RSX_GPU_VFETCH=1).
echo Para voltar ao normal, feche o jogo e use o atalho habitual.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal
