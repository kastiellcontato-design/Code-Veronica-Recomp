@echo off
setlocal
set "CV_FRAME_DETAIL=1"
set "CV_PRODUCER_PROFILE=1"
echo DIAGNOSTICO DO PRODUTOR: traducao de comandos e esperas.
echo Selecione a versao japonesa em 1920x1080.
echo Fique na sala dos zumbis junto ao portao e escadas por 30 segundos.
echo Mantenha a mesma camera e evite matar inimigos durante a medicao.
echo Aperte F12 uma vez no final e avise que terminou.
echo Medicoes de diagnostico; este atalho ainda nao e uma nova otimizacao.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
