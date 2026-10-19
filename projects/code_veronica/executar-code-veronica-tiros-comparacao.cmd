@echo off
setlocal
rem Diagnostic comparison: wait for missing fragment pipelines instead of skipping pending draws.
set "HITCHDBG=1"
set "PERF=1"
set "PS3_TIMELINE=1"
set "PSOMISSDBG=1"
set "FP_ASYNC=0"
echo COMPARACAO DOS TIROS: preparacao de efeitos sem omitir desenhos pendentes.
echo Pode haver uma pausa maior no primeiro efeito. Este atalho e so para diagnostico.
echo Entre em uma sala nova, atire e avise se a piscada cinza ocorreu.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal