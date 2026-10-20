@echo off
setlocal
set "CV_FIFO_NOTIFY=1"
set "CV_FIFO_WAKE=1"
set "CV_FIFO_PROFILE=1"
set "CV_FRAME_DETAIL=1"
set "CV_PRODUCER_PROFILE=1"
echo TESTE: registro SPU corrigido e aviso apos publicacao dos comandos.
echo Selecione a japonesa, 1080p. Carregue o segundo save, na mesma camera.
echo Espere 30 segundos, aperte F12 uma vez e deixe rodar por mais 5 segundos.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
