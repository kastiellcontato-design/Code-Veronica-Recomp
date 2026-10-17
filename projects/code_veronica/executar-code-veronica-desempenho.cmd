@echo off
setlocal
rem Same DXC entry point/settings as the user's run, plus the performance
rem reports: [PERF] (time per stage and CPU per thread, every 20 frames) and
rem [TL] (frame timeline, every 5 s). Use it only to measure a slow place;
rem these reports make the game hitch a little.
set "PERF=1"
set "PS3_TIMELINE=1"
rem [BUDGET]: vertices/draws asked per frame and how many did not fit.
set "FRAME_BUDGET=1"
echo Diagnostico de desempenho: va ate o lugar lento e fique ali uns 20 segundos.
call "%~dp0executar-code-veronica-dxc-experimental.cmd"
endlocal
