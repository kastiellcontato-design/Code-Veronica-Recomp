@echo off
setlocal
set "CV_POSE_CAP_PRECHECK=0"
echo COMPARACAO: busca anterior, sem a rejeicao antecipada de cadeias acima do limite.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
