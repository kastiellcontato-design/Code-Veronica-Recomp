@echo off
setlocal
rem TESTE de teto de FPS (versao JP). Sem limite de 60, sem VSync, sem geracao
rem de quadros e sem interpolacao: a logica do jogo acompanha os FPS, entao o
rem jogo fica ACELERADO. Serve so para medir ate onde a maquina chega antes e
rem depois de uma otimizacao. O atalho normal continua igual.
set "CV_FPS_DIAGNOSTIC=1"
set "CV_FPS_UNLOCKED=1"
set "CV_FPS_TEST_60=1"
set "CV_RENDER_INTERPOLATION="
set "CV_FPS_BENCHMARK_RAW=1"
set "CV_FSR_FRAME_GENERATION="
set "VSYNC="
set "CV_FSR_LAUNCHER_JP=1"
rem Reaproveita threads das tarefas de SPU em vez de criar uma por tarefa
rem (so neste teste; para comparar sem, troque 1 por 0).
set "SPU_TASK_POOL=1"
rem [TL] no log a cada 5 s (FPS e tempo por etapa). Nao usar PERF=1: causa trancos.
set "PS3_TIMELINE=1"
echo TESTE FPS LIVRE: sem limite de quadros. O jogo fica acelerado de proposito.
echo Para jogar normalmente, feche e use o atalho de sempre.
call "%~dp0executar-code-veronica-otimizado.cmd"
endlocal
