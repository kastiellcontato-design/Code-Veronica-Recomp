@echo off
setlocal
rem 60 FPS como antes do replay: o jogo desenha a cada quadro (lote proprio
rem para o quadro intermediario). Para comparar com o atalho japones normal.
set "CV_POSE_REPLAY=0"
call "%~dp0executar-code-veronica-japones.cmd"
endlocal
