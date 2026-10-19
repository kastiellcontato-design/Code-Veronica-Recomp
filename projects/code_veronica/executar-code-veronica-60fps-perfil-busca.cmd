@echo off
setlocal
set "CV_POSE_SEARCH_PROFILE=1"
echo DIAGNOSTICO DA BUSCA: amostragem, pode adicionar pequenas oscilacoes.
echo Use JP/DXC, 1080p, mesma sala/camera e isqueiro ligado por 30 segundos.
echo Depois aperte F12. Para comparar FPS, use o CMD CPU normal sem este probe.
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
