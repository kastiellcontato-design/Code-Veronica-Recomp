@echo off
setlocal
set "CV_FIFO_PROFILE=1"
set "CV_FRAME_DETAIL=1"
set "CV_PRODUCER_PROFILE=1"
echo DIAGNOSTICO DA ENTREGA DE COMANDOS - versao japonesa, 1080p.
echo Use a sala das caixas e escadas, mantendo a mesma camera por 30 segundos.
echo Aperte F12 uma vez e deixe rodar por mais 5 segundos.
echo Este teste mede a fila; nao altera o tempo de espera nem a ordem dos desenhos.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
