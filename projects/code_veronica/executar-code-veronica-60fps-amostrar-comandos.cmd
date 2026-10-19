@echo off
setlocal
set "CV_FRAME_DETAIL=1"
set "CV_PRODUCER_PROFILE=1"
set "PS3_SAMPLE=7"
set "PS3_SAMPLE_MAIN=1"
set "PS3_SAMPLE_THREAD=frame_clock"
set "PS3_SAMPLE_SPU="
echo AMOSTRAGEM: funcoes da thread que traduz os comandos graficos.
echo Selecione a versao japonesa em 1920x1080.
echo Fique na mesma camera da sala dos zumbis por 45 segundos.
echo Evite matar inimigos ou mudar de sala durante a coleta.
echo Aperte F12 uma vez ao final e avise que terminou.
echo O diagnostico pode alterar o FPS; este teste identifica funcoes caras.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
