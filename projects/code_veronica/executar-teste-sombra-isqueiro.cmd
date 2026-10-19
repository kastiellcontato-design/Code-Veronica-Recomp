@echo off
setlocal
set "CV_LIGHTER_PROBE=1"
set "CV_FPS_TEST_60="
set "CV_FPS_BENCHMARK_RAW="
set "CV_RENDER_INTERPOLATION="
echo DIAGNOSTICO: F10 alterna NORMAL / SEM SOMBRA. A luz continua acesa.
echo O modo aparece no titulo da janela. F12 salva a captura em cada modo.
echo Para voltar, feche e use o atalho normal. Nao altera o INI.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal
