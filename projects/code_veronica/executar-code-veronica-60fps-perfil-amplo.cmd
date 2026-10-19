@echo off
setlocal
set "CV_FRAME_DETAIL=1"
echo PERFIL AMPLO: amostras CPU/GPU por categoria, sem reduzir qualidade.
echo Na mesma camera: isqueiro desligado 20s, ligado 20s.
echo Depois repita em outra resolucao e fique parado 20s em cada teste.
echo F12 uma vez no final; nao aperte repetidamente durante a medicao.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd"
endlocal
