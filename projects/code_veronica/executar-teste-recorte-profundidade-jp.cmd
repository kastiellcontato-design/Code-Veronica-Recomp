@echo off
setlocal
rem Comparacao: recorte de profundidade antigo (so -w <= z <= w no vertex
rem shader). A grade aberta volta a aparecer como faixa escura na frente do
rem homem na primeira cutscene. Serve para comparar a agua da sala alagada.
set "RSX_ZCLIP=vs"
call "%~dp0executar-code-veronica-japones.cmd"
endlocal
