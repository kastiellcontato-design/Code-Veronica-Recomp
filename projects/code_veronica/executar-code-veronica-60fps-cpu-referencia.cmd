@echo off
setlocal
set "CV_POSE_COMPUTE_TEST=1"
set "CV_POSE_COMPUTE=0"
set "CV_FIFO_NOTIFY=0"
set "CV_FIFO_WAKE=0"
set "CV_FIFO_PROFILE="
set "CV_FRAME_DETAIL=1"
set "CV_PRODUCER_PROFILE=1"
call "%~dp0executar-code-veronica-60fps-interpolado.cmd" --cpu-blend
endlocal
