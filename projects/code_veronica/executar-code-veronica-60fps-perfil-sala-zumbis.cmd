@echo off
setlocal
set "CV_FRAME_DETAIL=1"
echo DIAGNOSTICO: preparacao dos desenhos por etapa, CPU e GPU.
echo Selecione a versao japonesa e mantenha 1920x1080.
echo Entre na sala dos zumbis junto ao portao e as escadas.
echo Fique na mesma camera por cerca de 30 segundos, sem trocar de sala.
echo Evite matar inimigos durante a amostra para manter uma carga parecida.
echo Aperte F12 uma vez no final e avise que terminou.
echo Este atalho apenas ativa medicoes; ainda nao e uma nova otimizacao.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
