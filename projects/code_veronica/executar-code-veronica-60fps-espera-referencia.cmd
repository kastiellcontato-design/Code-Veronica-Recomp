@echo off
setlocal
set "CV_FIFO_WAKE=0"
echo REFERENCIA: espera Sleep de 1 ms, mesma versao e qualidade.
call "%~dp0executar-code-veronica-60fps-perfil-produtor.cmd"
endlocal
