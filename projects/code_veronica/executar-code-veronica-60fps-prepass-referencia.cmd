@echo off
setlocal
set "CV_PREP_REPEAT=0"
echo COMPARACAO: consulta original dos pipelines, nova reutilizacao desativada.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
